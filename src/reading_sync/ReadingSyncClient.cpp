#include "ReadingSyncClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "ReadingStatsStore.h"
#include "ReadingSyncCredentialStore.h"
#include "ReadingSyncQueue.h"
#include "WifiCredentialStore.h"

namespace {
constexpr char SYNC_URL[] = "https://api.kimtoma.com/v1/reading/sync";
constexpr char VALIDATE_URL[] = "https://api.kimtoma.com/v1/reading/sync?validateOnly=1";
constexpr char COVER_URL_PREFIX[] = "https://api.kimtoma.com/v1/reading/books/";
constexpr char COVER_URL_SUFFIX[] = "/cover";
constexpr size_t MAX_SYNC_JSON_BYTES = 8192;
constexpr int HTTP_TIMEOUT_MS = 15000;
constexpr uint32_t WIFI_TIMEOUT_MS = 8000;

enum class CoverProcessingResult : uint8_t { Cleared, Retained, AuthenticationPaused };

bool isCancelled(const bool* cancelFlag) { return cancelFlag != nullptr && *cancelFlag; }

class NetworkLifecycle final {
 public:
  ~NetworkLifecycle() {
    if (wifiStarted_) {
      WiFi.disconnect(false);
      delay(100);
      WiFi.mode(WIFI_OFF);
      // ESP_ERR_WIFI_NOT_INIT is harmless when association failed before the
      // driver finished initialization.
      esp_wifi_deinit();
    }
    if (statsReleased_) {
      READING_STATS.reloadAfterNetwork();
    }
  }

  void markWifiStarted() { wifiStarted_ = true; }
  void markStatsReleased() { statsReleased_ = true; }

 private:
  bool wifiStarted_ = false;
  bool statsReleased_ = false;
};

bool connectSavedWifi(bool* cancelFlag) {
  if (WIFI_STORE.getCredentials().empty()) {
    WIFI_STORE.loadFromFile();
  }
  const std::string& lastConnectedSsid = WIFI_STORE.getLastConnectedSsid();
  if (lastConnectedSsid.empty()) {
    LOG_DBG("RSY", "No saved WiFi network is available");
    return false;
  }
  const WifiCredential* credential = WIFI_STORE.findCredential(lastConnectedSsid);
  if (credential == nullptr) {
    LOG_DBG("RSY", "The last WiFi network has no saved credential");
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  if (isCancelled(cancelFlag)) {
    return false;
  }
  if (credential->password.empty()) {
    WiFi.begin(credential->ssid.c_str());
  } else {
    WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  }

  const uint32_t startedAt = millis();
  while (millis() - startedAt < WIFI_TIMEOUT_MS) {
    if (isCancelled(cancelFlag)) {
      return false;
    }
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      return true;
    }
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
      return false;
    }
    delay(50);
  }
  return false;
}

bool isPathSegmentUnreserved(const uint8_t character) {
  return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '-' || character == '.' || character == '_' ||
         character == '~';
}

std::string urlEncodePathSegment(const std::string& value) {
  static constexpr char HEX_DIGITS[] = "0123456789ABCDEF";
  // bookId is capped at 128 Worker string units (at most 512 UTF-8 bytes). The
  // encoded URL therefore stays below 1,536 bytes and must be contiguous for
  // esp_http_client.
  std::string encoded;
  encoded.reserve(value.size() * 3);
  for (const uint8_t character : value) {
    if (isPathSegmentUnreserved(character)) {
      encoded.push_back(static_cast<char>(character));
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(HEX_DIGITS[character >> 4]);
    encoded.push_back(HEX_DIGITS[character & 0x0f]);
  }
  return encoded;
}

bool serializeMetadata(const ReadingSyncMetadata& metadata, std::string& payload) {
  if (!isReadingSyncMetadataBounded(metadata, true)) {
    LOG_ERR("RSY", "Reading sync metadata violates the wire bounds");
    return false;
  }

  // ArduinoJson 7's compatibility document owns a dynamic pool. It is bounded
  // to the Worker limit and exists only while the one-shot network task runs.
  StaticJsonDocument<MAX_SYNC_JSON_BYTES> document;
  document["schemaVersion"] = metadata.schemaVersion;
  document["sequence"] = metadata.sequence;
  document["bookId"] = metadata.bookId.c_str();
  document["title"] = metadata.title.c_str();
  document["author"] = metadata.author.c_str();
  document["progressPercent"] = metadata.progressPercent;
  if (!metadata.lastReadAt.empty()) {
    document["lastReadAt"] = metadata.lastReadAt.c_str();
  }
  if (!metadata.isbn13.empty()) {
    document["isbn13"] = metadata.isbn13.c_str();
  }
  if (!metadata.coverSha256.empty()) {
    document["coverSha256"] = metadata.coverSha256.c_str();
  }
  if (!metadata.coverMime.empty()) {
    document["coverMime"] = metadata.coverMime.c_str();
  }

  const size_t serializedSize = measureJson(document);
  if (document.overflowed() || serializedSize == 0 || serializedSize >= MAX_SYNC_JSON_BYTES) {
    LOG_ERR("RSY", "Reading sync metadata exceeds the JSON limit");
    return false;
  }

  // The request API needs contiguous UTF-8. Reserve the measured size once so
  // this cold path performs one bounded allocation rather than repeated growth.
  payload.clear();
  payload.reserve(serializedSize);
  return serializeJson(document, payload) == serializedSize;
}

bool parseSyncResponse(Stream& stream, const uint32_t expectedSequence, ReadingSyncResponse& response) {
  StaticJsonDocument<512> document;
  const DeserializationError error = deserializeJson(document, stream);
  if (error || document.overflowed() || !document.is<JsonObjectConst>()) {
    return false;
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();
  if (root.size() != 4) {
    return false;
  }

  const JsonVariantConst status = root["status"];
  const JsonVariantConst sequence = root["sequence"];
  const JsonVariantConst lastAcceptedSequence = root["lastAcceptedSequence"];
  const JsonVariantConst coverRequired = root["coverRequired"];
  ReadingSyncWireResponse wire;
  wire.hasStatus = status.is<const char*>();
  if (wire.hasStatus) {
    wire.status = status.as<const char*>();
  }
  wire.hasSequence = sequence.is<uint64_t>();
  if (wire.hasSequence) {
    wire.sequence = sequence.as<uint64_t>();
  }
  wire.hasLastAcceptedSequence = lastAcceptedSequence.is<uint64_t>();
  if (wire.hasLastAcceptedSequence) {
    wire.lastAcceptedSequence = lastAcceptedSequence.as<uint64_t>();
  }
  wire.hasCoverRequired = coverRequired.is<bool>();
  if (wire.hasCoverRequired) {
    wire.coverRequired = coverRequired.as<bool>();
  }
  return validateReadingSyncResponse(wire, expectedSequence, response);
}

bool parseAuthenticationResponse(Stream& stream) {
  StaticJsonDocument<128> document;
  const DeserializationError error = deserializeJson(document, stream);
  if (error || document.overflowed() || !document.is<JsonObjectConst>()) {
    return false;
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();
  const JsonVariantConst status = root["status"];
  return root.size() == 1 && status.is<const char*>() && std::strcmp(status.as<const char*>(), "authenticated") == 0;
}

bool coverMatchesMetadata(const ReadingCoverJob& cover, const ReadingSyncMetadata& metadata) {
  return matchesReadingCoverJob(cover, metadata.bookId, metadata.coverSha256) && cover.mime == metadata.coverMime;
}

void removeClearedCoverFile(const std::string& path) {
  if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
    LOG_ERR("RSY", "Could not remove a completed staged cover");
  }
}

bool clearCoverAndFile(ReadingSyncQueue& queue, const ReadingCoverJob& cover) {
  if (!queue.clearCoverJob(cover.bookId, cover.sha256)) {
    LOG_ERR("RSY", "Could not persist completed cover state");
    return false;
  }
  removeClearedCoverFile(cover.path);
  return true;
}

CoverProcessingResult processCoverJob(ReadingSyncClient& client, ReadingSyncQueue& queue, const std::string& token,
                                      bool* cancelFlag) {
  const ReadingCoverJob* pendingCover = queue.coverPending();
  if (pendingCover == nullptr) {
    return CoverProcessingResult::Cleared;
  }

  // The queue may mutate after the request. Keep one bounded snapshot so the
  // exact validated path can be deleted only after atomic queue persistence.
  const ReadingCoverJob cover = *pendingCover;
  const HttpDownloader::HttpResult result = client.uploadCover(cover, token, cancelFlag);
  if (result.error == HttpDownloader::ABORTED) {
    return CoverProcessingResult::Retained;
  }
  switch (result.statusCode) {
    case 200:
      if (result.error != HttpDownloader::OK) {
        return CoverProcessingResult::Retained;
      }
      return clearCoverAndFile(queue, cover) ? CoverProcessingResult::Cleared : CoverProcessingResult::Retained;
    case 400:
    case 413:
    case 422:
      if (result.error != HttpDownloader::HTTP_ERROR) {
        return CoverProcessingResult::Retained;
      }
      return clearCoverAndFile(queue, cover) ? CoverProcessingResult::Cleared : CoverProcessingResult::Retained;
    case 401:
    case 403:
      if (result.error != HttpDownloader::AUTH_FAILED) {
        return CoverProcessingResult::Retained;
      }
      queue.pauseAuthentication();
      return CoverProcessingResult::AuthenticationPaused;
    default:
      return CoverProcessingResult::Retained;
  }
}

const char* terminalReasonForStatus(const int statusCode) {
  switch (statusCode) {
    case 400:
      return "bad_request";
    case 413:
      return "payload_too_large";
    case 422:
      return "unprocessable";
    default:
      return nullptr;
  }
}
}  // namespace

ReadingSyncClient& ReadingSyncClient::getInstance() {
  static ReadingSyncClient instance;
  return instance;
}

HttpDownloader::HttpResult ReadingSyncClient::validate(const std::string& token, bool* cancelFlag) {
  const std::string emptyJson = "{}";
  return HttpDownloader::postJsonWithStatus(
      VALIDATE_URL, emptyJson, token, [](Stream& stream) { return parseAuthenticationResponse(stream); },
      HTTP_TIMEOUT_MS, cancelFlag);
}

HttpDownloader::HttpResult ReadingSyncClient::sync(const ReadingSyncMetadata& metadata, const std::string& token,
                                                   ReadingSyncResponse& response, bool* cancelFlag) {
  std::string payload;
  if (!serializeMetadata(metadata, payload)) {
    return {HttpDownloader::HTTP_ERROR, 0};
  }

  return HttpDownloader::postJsonWithStatus(
      SYNC_URL, payload, token,
      [&response, expectedSequence = metadata.sequence](Stream& stream) {
        return parseSyncResponse(stream, expectedSequence, response);
      },
      HTTP_TIMEOUT_MS, cancelFlag);
}

HttpDownloader::HttpResult ReadingSyncClient::uploadCover(const ReadingCoverJob& cover, const std::string& token,
                                                          bool* cancelFlag) {
  if (!isReadingCoverJobValid(cover)) {
    return {HttpDownloader::FILE_ERROR, 0};
  }

  const std::string encodedBookId = urlEncodePathSegment(cover.bookId);
  std::string url;
  url.reserve(sizeof(COVER_URL_PREFIX) - 1 + encodedBookId.size() + sizeof(COVER_URL_SUFFIX) - 1);
  url.append(COVER_URL_PREFIX).append(encodedBookId).append(COVER_URL_SUFFIX);
  return HttpDownloader::putFileWithStatus(
      url, cover.path, cover.mime, cover.sha256, token, [](Stream&) { return true; }, HTTP_TIMEOUT_MS, cancelFlag,
      1024);
}

void ReadingSyncClient::performPendingSync(ReadingSyncQueue& queue, ReadingSyncCredentialStore& credentials,
                                           bool* cancelFlag) {
  if (isCancelled(cancelFlag) || queue.isCorrupt() || queue.authenticationPaused() || !credentials.hasToken() ||
      (queue.pending() == nullptr && queue.coverPending() == nullptr)) {
    return;
  }

  NetworkLifecycle networkLifecycle;
  networkLifecycle.markWifiStarted();
  if (!connectSavedWifi(cancelFlag)) {
    return;
  }
  if (!READING_STATS.releaseMemoryForNetwork()) {
    return;
  }
  networkLifecycle.markStatsReleased();

  const std::string& token = credentials.tokenForRequest();
  const ReadingSyncMetadata* pending = queue.pending();
  if (pending == nullptr) {
    processCoverJob(*this, queue, token, cancelFlag);
    return;
  }
  if (isCancelled(cancelFlag)) {
    return;
  }

  const uint32_t requestSequence = pending->sequence;
  ReadingSyncResponse response;
  const HttpDownloader::HttpResult result = sync(*pending, token, response, cancelFlag);
  if (result.statusCode == 200 && result.error == HttpDownloader::OK) {
    const ReadingCoverJob* pendingCover = queue.coverPending();
    std::string stagedCoverPath;
    if (pendingCover != nullptr) {
      stagedCoverPath = pendingCover->path;
    }
    const bool matchingCover = pendingCover != nullptr && coverMatchesMetadata(*pendingCover, *pending);
    const bool keepCover = pendingCover != nullptr;
    if (!queue.applyServerResult(requestSequence, response.lastAcceptedSequence, response.status, keepCover)) {
      LOG_ERR("RSY", "Could not persist accepted reading metadata state");
      return;
    }

    pendingCover = queue.coverPending();
    if (pendingCover == nullptr) {
      if (!stagedCoverPath.empty()) {
        removeClearedCoverFile(stagedCoverPath);
      }
      return;
    }
    if (!matchingCover || response.coverRequired) {
      processCoverJob(*this, queue, token, cancelFlag);
      return;
    }

    const ReadingCoverJob cover = *pendingCover;
    clearCoverAndFile(queue, cover);
    return;
  }

  if ((result.statusCode == 401 || result.statusCode == 403) && result.error == HttpDownloader::AUTH_FAILED) {
    queue.pauseAuthentication();
    return;
  }

  const char* terminalReason = terminalReasonForStatus(result.statusCode);
  if (terminalReason == nullptr || result.error != HttpDownloader::HTTP_ERROR) {
    return;
  }

  const ReadingCoverJob* pendingCover = queue.coverPending();
  if (pendingCover != nullptr && !coverMatchesMetadata(*pendingCover, *pending) &&
      processCoverJob(*this, queue, token, cancelFlag) != CoverProcessingResult::Cleared) {
    return;
  }

  std::string stagedCoverPath;
  if (const ReadingCoverJob* cover = queue.coverPending(); cover != nullptr) {
    stagedCoverPath = cover->path;
  }
  if (queue.dropTerminal(requestSequence, terminalReason) && !stagedCoverPath.empty()) {
    removeClearedCoverFile(stagedCoverPath);
  }
}
