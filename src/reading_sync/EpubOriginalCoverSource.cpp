#include "EpubOriginalCoverSource.h"

#ifdef CROSSPOINT_EMULATED

bool stageOriginalEpubCover(const Epub&, const std::string&, ReadingCoverJob& out, bool* createdNewFile) {
  out = {};
  if (createdNewFile != nullptr) {
    *createdNewFile = false;
  }
  return false;
}

#else

#include <Epub.h>
#include <HalStorage.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <utility>

namespace {
constexpr char READING_SYNC_DIRECTORY[] = "/.crosspoint/reading_sync";
constexpr char COVER_DIRECTORY[] = "/.crosspoint/reading_sync/covers";
constexpr char COVER_TEMP_PATH[] = "/.crosspoint/reading_sync/covers/.tmp";
constexpr size_t COVER_STREAM_CHUNK_SIZE = 1024;
constexpr size_t SHA256_BYTES = 32;
constexpr size_t SHA256_HEX_CHARS = SHA256_BYTES * 2;
constexpr size_t COVER_PREFIX_BYTES = 8;

class OriginalCoverSink final : public Print {
 public:
  explicit OriginalCoverSink(HalFile& file) : file_(file) {
    mbedtls_sha256_init(&sha256_);
    healthy_ = mbedtls_sha256_starts(&sha256_, 0) == 0;
  }

  ~OriginalCoverSink() override { mbedtls_sha256_free(&sha256_); }

  size_t write(const uint8_t byte) override { return write(&byte, 1); }

  size_t write(const uint8_t* data, const size_t size) override {
    if (!healthy_ || data == nullptr || size > 2097152 - bytesWritten_) {
      healthy_ = false;
      return 0;
    }

    const size_t written = file_.write(data, size);
    if (written != size) {
      healthy_ = false;
      return written;
    }
    if (mbedtls_sha256_update(&sha256_, data, size) != 0) {
      healthy_ = false;
      return 0;
    }

    const size_t prefixRemaining = COVER_PREFIX_BYTES - prefixSize_;
    const size_t prefixToCopy = std::min(prefixRemaining, size);
    std::copy_n(data, prefixToCopy, prefix_.begin() + prefixSize_);
    prefixSize_ += prefixToCopy;
    bytesWritten_ += size;
    return size;
  }

  bool finish(std::array<uint8_t, SHA256_BYTES>& digest) {
    if (!healthy_ || mbedtls_sha256_finish(&sha256_, digest.data()) != 0) {
      healthy_ = false;
      return false;
    }
    return true;
  }

  bool healthy() const { return healthy_; }
  size_t bytesWritten() const { return bytesWritten_; }
  size_t prefixSize() const { return prefixSize_; }
  const std::array<uint8_t, COVER_PREFIX_BYTES>& prefix() const { return prefix_; }

 private:
  HalFile& file_;
  mbedtls_sha256_context sha256_ = {};
  std::array<uint8_t, COVER_PREFIX_BYTES> prefix_ = {};
  size_t bytesWritten_ = 0;
  size_t prefixSize_ = 0;
  bool healthy_ = false;
};

bool removeTemporaryCover() { return !Storage.exists(COVER_TEMP_PATH) || Storage.remove(COVER_TEMP_PATH); }

std::string digestToLowerHex(const std::array<uint8_t, SHA256_BYTES>& digest) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  // The hash is persisted in ReadingCoverJob, so use one bounded 64-byte heap
  // allocation and reserve it before the append loop instead of a stack buffer.
  std::string result;
  result.reserve(SHA256_HEX_CHARS);
  for (const uint8_t byte : digest) {
    result.push_back(HEX_DIGITS[byte >> 4]);
    result.push_back(HEX_DIGITS[byte & 0x0f]);
  }
  return result;
}
}  // namespace

bool stageOriginalEpubCover(const Epub& epub, const std::string& bookId, ReadingCoverJob& out, bool* createdNewFile) {
  out = {};
  if (createdNewFile != nullptr) {
    *createdNewFile = false;
  }
  if (!removeTemporaryCover() || bookId.empty()) {
    return false;
  }

  const std::string& coverItemHref = epub.getOriginalCoverItemHref();
  size_t declaredSize = 0;
  if (coverItemHref.empty() || !epub.getItemSize(coverItemHref, &declaredSize) ||
      !isReadingCoverSizeAllowed(declaredSize)) {
    return false;
  }

  Storage.mkdir(READING_SYNC_DIRECTORY);
  Storage.mkdir(COVER_DIRECTORY);

  size_t actualSize = 0;
  size_t prefixSize = 0;
  std::array<uint8_t, COVER_PREFIX_BYTES> prefix = {};
  std::array<uint8_t, SHA256_BYTES> digest = {};
  bool opened = false;
  bool staged = false;
  {
    HalFile temporaryCover;
    opened = Storage.openFileForWrite("RSY", COVER_TEMP_PATH, temporaryCover);
    if (opened) {
      OriginalCoverSink sink(temporaryCover);
      const bool streamAndHashComplete = sink.healthy() &&
                                         epub.readItemContentsToStream(coverItemHref, sink, COVER_STREAM_CHUNK_SIZE) &&
                                         sink.finish(digest);
      temporaryCover.flush();
      actualSize = sink.bytesWritten();
      prefixSize = sink.prefixSize();
      prefix = sink.prefix();
      const size_t persistedSize = temporaryCover.fileSize();
      const bool closeSucceeded = temporaryCover.close();
      staged = streamAndHashComplete && isReadingCoverPersistenceComplete(actualSize, persistedSize, closeSucceeded);
    }
  }

  const std::string mime = detectReadingCoverMime(prefix.data(), prefixSize);
  if (!opened || !staged || actualSize != declaredSize || !isReadingCoverSizeAllowed(actualSize) || mime.empty()) {
    removeTemporaryCover();
    return false;
  }

  // These bounded strings become queue-owned state after this cold one-shot
  // path; reserve once so construction does not repeatedly fragment the heap.
  std::string sha256 = digestToLowerHex(digest);
  std::string targetPath;
  targetPath.reserve(sizeof(COVER_DIRECTORY) + SHA256_HEX_CHARS + 4);
  targetPath.append(COVER_DIRECTORY).append("/").append(sha256).append(mime == "image/jpeg" ? ".jpg" : ".png");

  if (Storage.exists(targetPath.c_str())) {
    if (!removeTemporaryCover()) {
      return false;
    }
  } else if (!Storage.rename(COVER_TEMP_PATH, targetPath.c_str())) {
    removeTemporaryCover();
    return false;
  } else if (createdNewFile != nullptr) {
    *createdNewFile = true;
  }

  out.bookId = bookId;
  out.sha256 = std::move(sha256);
  out.mime = mime;
  out.path = std::move(targetPath);
  return true;
}

#endif
