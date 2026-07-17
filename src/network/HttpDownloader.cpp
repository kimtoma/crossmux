#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <Stream.h>
#include <base64.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

namespace {
// RX holds the response headers. 4096 fits real OPDS servers; GitHub's release
// CDN sends more and logs HTTP_HEADER "Buffer length is small", but that's
// non-fatal: the headers we read (Location, Content-Length) come first and
// survive. Smaller keeps contiguous heap free while WiFi and TLS are up. TX
// carries the request body for POST (and just the request line for GET); the
// response body streams in READ_CHUNK pieces.
constexpr int HTTP_RX_BUF = 4096;
constexpr int HTTP_TX_BUF = 1024;
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room. esp_http_client's timeout_ms is uint32, so unlike Arduino
// HTTPClient's uint16 setTimeout it doesn't silently truncate.
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr size_t READ_CHUNK = 2048;

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;  // returns false to abort the transfer
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
};

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Streams a GET body through sink.write in READ_CHUNK pieces. Uses the manual
// open/fetch_headers/read path rather than esp_http_client_perform(): perform()
// pushes the whole body through an event callback and reports a chunked body
// that ends early as ESP_ERR_HTTP_INCOMPLETE_DATA, whereas the read loop streams
// large/slow files and surfaces a short read directly.
HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     Sink& sink) {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  // Verify HTTPS against the bundled CA roots. This build has esp-tls
  // CONFIG_ESP_TLS_INSECURE off, so an unverified TLS handshake can't be set
  // up at all; the model is public servers over verified https and local
  // servers over plain http (esp_http_client picks the transport from the URL
  // scheme, so http:// needs no cert config). The prior setInsecure() worked
  // only because Arduino's ssl_client drives mbedtls directly.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "client init failed");
    return HttpDownloader::HTTP_ERROR;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (!username.empty() && !password.empty()) {
    // Preemptive Basic auth, like the prior addHeader; don't wait for a 401.
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }

  // open()/read() does not auto-follow redirects (only perform() does), so step
  // 30x responses manually. OPDS download endpoints and the GitHub release CDN
  // both redirect.
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }
  int64_t contentLength = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  for (int hop = 0; isRedirect(status) && hop < 5; ++hop) {
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "redirect open failed: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
  }
  if (status != 200) {
    LOG_ERR("HTTP", "unexpected status: %d", status);
    esp_http_client_cleanup(client);
    // 401/403: missing or wrong Basic auth (OPDS / ryOS Books).
    if (status == 401 || status == 403) return HttpDownloader::AUTH_FAILED;
    return HttpDownloader::HTTP_ERROR;
  }

  // fetch_headers returns 0 for a chunked response (no Content-Length); leave
  // total at 0 so progress stays silent and the size check is skipped.
  sink.total = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;

  auto buf = makeUniqueNoThrow<char[]>(READ_CHUNK);
  if (!buf) {
    LOG_ERR("HTTP", "OOM: %u byte read buffer", (unsigned)READ_CHUNK);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  while (true) {
    if (sink.cancelFlag && *sink.cancelFlag) {
      esp_http_client_cleanup(client);
      return HttpDownloader::ABORTED;
    }
    const int read = esp_http_client_read(client, buf.get(), READ_CHUNK);
    if (read < 0) {
      LOG_ERR("HTTP", "read error after %zu bytes", sink.downloaded);
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (read == 0) break;  // all data received
    if (!sink.write(reinterpret_cast<const uint8_t*>(buf.get()), read)) {
      esp_http_client_cleanup(client);
      return HttpDownloader::FILE_ERROR;
    }
    sink.downloaded += read;
    if (sink.progress && sink.total > 0) sink.progress(sink.downloaded, sink.total);
  }

  const bool complete = esp_http_client_is_complete_data_received(client);
  esp_http_client_cleanup(client);
  if (!complete) {
    LOG_ERR("HTTP", "incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}

// Pull-style Stream wrapper around esp_http_client_read. Backed by a small
// refill buffer so each read()/peek() byte does not cost a syscall.
// esp_http_client_read already strips chunked transfer encoding, so the
// wrapper has no framing logic — read()==0 is the only EOF signal we need.
//
// setTimeout(0) makes Stream::timedRead bail immediately on -1 (our
// "no more data" code), so ArduinoJson stops as soon as it has parsed the
// closing token rather than spending the default 1s waiting for more input.
class EspHttpReadStream final : public Stream {
 public:
  EspHttpReadStream(esp_http_client_handle_t client, bool* cancelFlag)
      : client_(client), cancelFlag_(cancelFlag), buf_(makeUniqueNoThrow<char[]>(kBufSize)) {
    // The 1 KiB response buffer cannot safely live on the 4 KiB network task
    // stack. Allocate it once for this cold request path and release via RAII.
    if (!buf_) {
      LOG_ERR("HTTP", "OOM: %u byte response buffer", static_cast<unsigned>(kBufSize));
      error_ = HttpDownloader::HTTP_ERROR;
    }
    setTimeout(0);
  }

  int available() override { return static_cast<int>(len_ - pos_); }

  int read() override {
    if (pos_ >= len_ && !refill()) return -1;
    return static_cast<unsigned char>(buf_[pos_++]);
  }

  int peek() override {
    if (pos_ >= len_ && !refill()) return -1;
    return static_cast<unsigned char>(buf_[pos_]);
  }

  size_t write(uint8_t) override { return 0; }
  void flush() override {}
  bool healthy() const { return buf_ != nullptr; }
  HttpDownloader::DownloadError error() const { return error_; }

 private:
  static constexpr size_t kBufSize = 1024;

  bool refill() {
    if (!buf_ || (cancelFlag_ != nullptr && *cancelFlag_)) {
      error_ = buf_ ? HttpDownloader::ABORTED : HttpDownloader::HTTP_ERROR;
      return false;
    }
    const int n = esp_http_client_read(client_, buf_.get(), static_cast<int>(kBufSize));
    if (n < 0) {
      LOG_ERR("HTTP", "read error mid-body");
      error_ = HttpDownloader::HTTP_ERROR;
      return false;
    }
    if (n == 0) return false;  // server-side EOF
    pos_ = 0;
    len_ = static_cast<size_t>(n);
    return true;
  }

  esp_http_client_handle_t client_;
  bool* cancelFlag_ = nullptr;
  std::unique_ptr<char[]> buf_;
  size_t pos_ = 0;
  size_t len_ = 0;
  HttpDownloader::DownloadError error_ = HttpDownloader::OK;
};

class HttpClientCleanup final {
 public:
  explicit HttpClientCleanup(const esp_http_client_handle_t client) : client_(client) {}
  ~HttpClientCleanup() {
    if (client_ != nullptr) {
      esp_http_client_cleanup(client_);
    }
  }

  HttpClientCleanup(const HttpClientCleanup&) = delete;
  HttpClientCleanup& operator=(const HttpClientCleanup&) = delete;

 private:
  esp_http_client_handle_t client_;
};

bool isCancelled(const bool* cancelFlag) { return cancelFlag != nullptr && *cancelFlag; }

void setBearerHeader(const esp_http_client_handle_t client, const std::string& bearerToken) {
  if (bearerToken.empty()) {
    return;
  }

  // The credential must be contiguous and null-terminated at the ESP-IDF C API
  // boundary. This one bounded network-lifetime string is released on return.
  std::string authHeader;
  authHeader.reserve(sizeof("Bearer ") - 1 + bearerToken.size());
  authHeader.append("Bearer ").append(bearerToken);
  esp_http_client_set_header(client, "Authorization", authHeader.c_str());
}

HttpDownloader::HttpResult consumeResponse(const esp_http_client_handle_t client, const int statusCode,
                                           const std::function<bool(Stream&)>& onResponse, bool* cancelFlag) {
  if (isCancelled(cancelFlag)) {
    return {HttpDownloader::ABORTED, statusCode};
  }

  EspHttpReadStream bodyStream(client, cancelFlag);
  if (!bodyStream.healthy()) {
    return {HttpDownloader::HTTP_ERROR, statusCode};
  }
  const bool consumerOk = !onResponse || onResponse(bodyStream);
  if (bodyStream.error() != HttpDownloader::OK) {
    return {bodyStream.error(), statusCode};
  }
  if (isCancelled(cancelFlag)) {
    return {HttpDownloader::ABORTED, statusCode};
  }
  if (statusCode == 401 || statusCode == 403) {
    return {HttpDownloader::AUTH_FAILED, statusCode};
  }
  if (statusCode != 200 || !consumerOk) {
    return {HttpDownloader::HTTP_ERROR, statusCode};
  }
  return {HttpDownloader::OK, statusCode};
}

HttpDownloader::HttpResult runPostJson(const std::string& url, const std::string& payload,
                                       const std::string& bearerToken, const std::function<bool(Stream&)>& onResponse,
                                       const int timeoutMs, bool* cancelFlag) {
  if (isCancelled(cancelFlag)) {
    return {HttpDownloader::ABORTED, 0};
  }
  if (payload.size() > static_cast<size_t>(INT_MAX)) {
    return {HttpDownloader::HTTP_ERROR, 0};
  }

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.timeout_ms = timeoutMs;
  // Verified HTTPS via the bundled CA roots — same trust posture as runGet.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.method = HTTP_METHOD_POST;
  config.keep_alive_enable = true;
#ifndef CROSSPOINT_EMULATED
  config.disable_auto_redirect = true;
#endif

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "POST client init failed");
    return {HttpDownloader::HTTP_ERROR, 0};
  }
  const HttpClientCleanup cleanup(client);
#ifdef CROSSPOINT_EMULATED
  client->follow_redirects = false;
#endif

  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  setBearerHeader(client, bearerToken);

  // open(content_length) reserves the body length for the request line;
  // write() then streams the payload. POST does not follow redirects here —
  // a 30x on a JSON RPC endpoint is a server misconfiguration we want to
  // surface, not silently re-POST against.
  if (isCancelled(cancelFlag)) {
    return {HttpDownloader::ABORTED, 0};
  }
  const esp_err_t err = esp_http_client_open(client, static_cast<int>(payload.size()));
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "POST open failed: %s", esp_err_to_name(err));
    return {HttpDownloader::HTTP_ERROR, 0};
  }

  size_t payloadOffset = 0;
  while (payloadOffset < payload.size()) {
    if (isCancelled(cancelFlag)) {
      return {HttpDownloader::ABORTED, 0};
    }
    const int written =
        esp_http_client_write(client, payload.data() + payloadOffset, static_cast<int>(payload.size() - payloadOffset));
    if (written <= 0) {
      LOG_ERR("HTTP", "POST request write failed");
      return {HttpDownloader::HTTP_ERROR, 0};
    }
    payloadOffset += static_cast<size_t>(written);
  }

  const int64_t contentLength = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (contentLength < 0) {
    LOG_ERR("HTTP", "POST response header read failed");
    return {HttpDownloader::HTTP_ERROR, 0};
  }
  return consumeResponse(client, status, onResponse, cancelFlag);
}

HttpDownloader::HttpResult runPutFile(const std::string& url, const std::string& path, const std::string& mime,
                                      const std::string& sha256, const std::string& bearerToken,
                                      const std::function<bool(Stream&)>& onResponse, const int timeoutMs,
                                      bool* cancelFlag, const size_t chunkSize) {
  constexpr size_t MAX_UPLOAD_CHUNK = 1024;
  if (isCancelled(cancelFlag)) {
    return {HttpDownloader::ABORTED, 0};
  }
  if (chunkSize == 0 || chunkSize > MAX_UPLOAD_CHUNK) {
    return {HttpDownloader::HTTP_ERROR, 0};
  }

  HalFile file;
  if (!Storage.openFileForRead("HTTP", path, file)) {
    LOG_ERR("HTTP", "PUT staged file is not readable");
    return {HttpDownloader::FILE_ERROR, 0};
  }
  const uint64_t fileSize = file.fileSize64();
  constexpr uint64_t MAX_COVER_BYTES = 2097152;
  if (fileSize == 0 || fileSize > MAX_COVER_BYTES || fileSize > static_cast<uint64_t>(INT_MAX)) {
    LOG_ERR("HTTP", "PUT staged file has invalid size");
    return {HttpDownloader::FILE_ERROR, 0};
  }

  // A network task cannot afford a 1 KiB local array. This single bounded
  // upload buffer is allocated after the file is validated and freed by RAII.
  auto uploadBuffer = makeUniqueNoThrow<char[]>(chunkSize);
  if (!uploadBuffer) {
    LOG_ERR("HTTP", "OOM: %u byte upload buffer", static_cast<unsigned>(chunkSize));
    return {HttpDownloader::HTTP_ERROR, 0};
  }

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.timeout_ms = timeoutMs;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.method = HTTP_METHOD_PUT;
  config.keep_alive_enable = true;
#ifndef CROSSPOINT_EMULATED
  config.disable_auto_redirect = true;
#endif

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "PUT client init failed");
    return {HttpDownloader::HTTP_ERROR, 0};
  }
  const HttpClientCleanup cleanup(client);
#ifdef CROSSPOINT_EMULATED
  client->follow_redirects = false;
#endif

  char contentLength[24] = {};
  std::snprintf(contentLength, sizeof(contentLength), "%llu", static_cast<unsigned long long>(fileSize));
  esp_http_client_set_header(client, "Content-Type", mime.c_str());
  esp_http_client_set_header(client, "Content-Length", contentLength);
  esp_http_client_set_header(client, "X-Cover-SHA256", sha256.c_str());
  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  setBearerHeader(client, bearerToken);

  if (isCancelled(cancelFlag)) {
    return {HttpDownloader::ABORTED, 0};
  }
  const esp_err_t err = esp_http_client_open(client, static_cast<int>(fileSize));
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "PUT open failed: %s", esp_err_to_name(err));
    return {HttpDownloader::HTTP_ERROR, 0};
  }

  uint64_t uploaded = 0;
  while (uploaded < fileSize) {
    if (isCancelled(cancelFlag)) {
      return {HttpDownloader::ABORTED, 0};
    }
    const size_t bytesToRead = static_cast<size_t>(std::min<uint64_t>(chunkSize, fileSize - uploaded));
    const int bytesRead = file.read(uploadBuffer.get(), bytesToRead);
    if (bytesRead != static_cast<int>(bytesToRead)) {
      LOG_ERR("HTTP", "PUT staged file read failed");
      return {HttpDownloader::FILE_ERROR, 0};
    }

    size_t chunkOffset = 0;
    while (chunkOffset < bytesToRead) {
      if (isCancelled(cancelFlag)) {
        return {HttpDownloader::ABORTED, 0};
      }
      const int written =
          esp_http_client_write(client, &uploadBuffer[chunkOffset], static_cast<int>(bytesToRead - chunkOffset));
      if (written <= 0) {
        LOG_ERR("HTTP", "PUT request write failed");
        return {HttpDownloader::HTTP_ERROR, 0};
      }
      chunkOffset += static_cast<size_t>(written);
    }
    uploaded += bytesToRead;
  }

  const int64_t responseLength = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (responseLength < 0) {
    LOG_ERR("HTTP", "PUT response header read failed");
    return {HttpDownloader::HTTP_ERROR, 0};
  }
  return consumeResponse(client, status, onResponse, cancelFlag);
}
}  // namespace

HttpDownloader::DownloadError HttpDownloader::fetchUrl(const std::string& url, Stream& outContent,
                                                       const std::string& username, const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; };
  return runGet(url, username, password, sink);
}

HttpDownloader::DownloadError HttpDownloader::fetchUrl(const std::string& url, std::string& outContent,
                                                       const std::string& username, const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  outContent.clear();  // start clean; the sink appends, so don't carry prior content
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) {
    outContent.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  return runGet(url, username, password, sink);
}

HttpDownloader::DownloadError HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData,
                                                       const std::string& username, const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Sink sink;
  sink.write = onData;
  return runGet(url, username, password, sink);
}

bool HttpDownloader::postJson(const std::string& url, const std::string& payload, const std::string& bearerToken,
                              const std::function<bool(Stream&)>& onResponse, int timeoutMs) {
  LOG_DBG("HTTP", "POST: %s (body=%u bytes)", url.c_str(), static_cast<unsigned>(payload.size()));
  const HttpResult result = postJsonWithStatus(url, payload, bearerToken, onResponse, timeoutMs);
  return result.error == OK && result.statusCode == 200;
}

HttpDownloader::HttpResult HttpDownloader::postJsonWithStatus(const std::string& url, const std::string& payload,
                                                              const std::string& bearerToken,
                                                              const std::function<bool(Stream&)>& onResponse,
                                                              const int timeoutMs, bool* cancelFlag) {
  return runPostJson(url, payload, bearerToken, onResponse, timeoutMs, cancelFlag);
}

HttpDownloader::HttpResult HttpDownloader::putFileWithStatus(const std::string& url, const std::string& path,
                                                             const std::string& mime, const std::string& sha256,
                                                             const std::string& bearerToken,
                                                             const std::function<bool(Stream&)>& onResponse,
                                                             const int timeoutMs, bool* cancelFlag,
                                                             const size_t chunkSize) {
  return runPutFile(url, path, mime, sha256, bearerToken, onResponse, timeoutMs, cancelFlag, chunkSize);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  if (Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };

  const DownloadError result = runGet(url, username, password, sink);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove.
  file.close();

  if (result != OK) {
    Storage.remove(destPath.c_str());
    return result;
  }
  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);
  return OK;
}
