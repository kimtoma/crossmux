#include "ReadingSyncQueue.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <climits>
#include <utility>

#include "ReadingSyncPolicy.h"

namespace {
constexpr char QUEUE_DIRECTORY[] = "/.crosspoint/reading_sync";
constexpr char COVER_DIRECTORY[] = "/.crosspoint/reading_sync/covers";
constexpr char QUEUE_PATH[] = "/.crosspoint/reading_sync/queue.json";
constexpr char QUEUE_TEMP_PATH[] = "/.crosspoint/reading_sync/queue.json.tmp";
constexpr char QUEUE_CORRUPT_PATH[] = "/.crosspoint/reading_sync/queue.json.corrupt";
constexpr size_t MAX_QUEUE_BYTES = 8192;

bool isTerminalReason(const std::string& reason) {
  return reason == "bad_request" || reason == "payload_too_large" || reason == "unprocessable" ||
         reason == "sequence_exhausted";
}

bool isHttpTerminalReason(const std::string& reason) {
  return reason == "bad_request" || reason == "payload_too_large" || reason == "unprocessable";
}

bool isRequiredString(const JsonObjectConst& object, const char* key, const bool allowEmpty = false) {
  if (!object[key].is<const char*>()) {
    return false;
  }
  const char* value = object[key].as<const char*>();
  return allowEmpty || (value != nullptr && value[0] != '\0');
}

bool isOptionalString(const JsonObjectConst& object, const char* key) {
  return object[key].isNull() || object[key].is<const char*>();
}

void preserveCorruptQueue() {
  if (Storage.exists(QUEUE_CORRUPT_PATH)) {
    LOG_ERR("RSQ", "Queue is corrupt; existing preserved copy prevents rename");
    return;
  }
  if (!Storage.rename(QUEUE_PATH, QUEUE_CORRUPT_PATH)) {
    LOG_ERR("RSQ", "Queue is corrupt and could not be preserved");
  }
}
}  // namespace

ReadingSyncQueue& ReadingSyncQueue::getInstance() {
  static ReadingSyncQueue instance;
  return instance;
}

bool ReadingSyncQueue::loadFromFile() {
  if (!Storage.exists(QUEUE_PATH)) {
    return true;
  }

  // ArduinoJson 7's StaticJsonDocument compatibility type allocates its pool
  // dynamically. This cold load path first caps the input file at 8 KiB and
  // streams it directly from HalFile, avoiding an 8 KiB task-stack buffer.
  StaticJsonDocument<MAX_QUEUE_BYTES> document;
  bool parsed = false;
  {
    HalFile file;
    if (!Storage.openFileForRead("RSQ", QUEUE_PATH, file)) {
      return false;
    }
    const size_t fileSize = file.fileSize();
    if (fileSize > 0 && fileSize <= MAX_QUEUE_BYTES) {
      const DeserializationError error = deserializeJson(document, file);
      parsed = !error && !document.overflowed();
    }
  }

  ReadingSyncQueue loaded;
  if (parsed && document.is<JsonObjectConst>()) {
    const JsonObjectConst root = document.as<JsonObjectConst>();
    parsed = root["schemaVersion"].is<uint32_t>() && root["schemaVersion"].as<uint32_t>() == kSchemaVersion &&
             root["nextSequence"].is<uint32_t>() && root["nextSequence"].as<uint32_t>() != 0 &&
             isRequiredString(root, "lastAcceptedFingerprint", true) && root["authPaused"].is<bool>() &&
             root["terminal"].is<bool>() && isRequiredString(root, "terminalReason", true);

    if (parsed) {
      loaded.nextSequence_ = root["nextSequence"].as<uint32_t>();
      loaded.lastAcceptedFingerprint_ = root["lastAcceptedFingerprint"].as<const char*>();
      loaded.authPaused_ = root["authPaused"].as<bool>();
      loaded.terminal_ = root["terminal"].as<bool>();
      loaded.terminalReason_ = root["terminalReason"].as<const char*>();
      parsed = (loaded.terminal_ && isTerminalReason(loaded.terminalReason_)) ||
               (!loaded.terminal_ && loaded.terminalReason_.empty());
    }

    if (parsed && !root["pending"].isNull()) {
      parsed = root["pending"].is<JsonObjectConst>();
      if (parsed) {
        const JsonObjectConst pending = root["pending"].as<JsonObjectConst>();
        parsed = pending["schemaVersion"].is<uint32_t>() &&
                 pending["schemaVersion"].as<uint32_t>() == ReadingSyncQueue::kSchemaVersion &&
                 pending["sequence"].is<uint32_t>() && pending["sequence"].as<uint32_t>() != 0 &&
                 isRequiredString(pending, "bookId") && isRequiredString(pending, "title") &&
                 isRequiredString(pending, "author", true) && pending["progressPercent"].is<uint32_t>() &&
                 pending["progressPercent"].as<uint32_t>() <= 100 && isOptionalString(pending, "lastReadAt") &&
                 isOptionalString(pending, "isbn13") && isOptionalString(pending, "coverSha256") &&
                 isOptionalString(pending, "coverMime");
        if (parsed) {
          loaded.hasPending_ = true;
          loaded.pending_.schemaVersion = pending["schemaVersion"].as<uint8_t>();
          loaded.pending_.sequence = pending["sequence"].as<uint32_t>();
          loaded.pending_.bookId = pending["bookId"].as<const char*>();
          loaded.pending_.title = pending["title"].as<const char*>();
          loaded.pending_.author = pending["author"].as<const char*>();
          loaded.pending_.progressPercent = pending["progressPercent"].as<uint8_t>();
          loaded.pending_.lastReadAt = pending["lastReadAt"] | "";
          loaded.pending_.isbn13 = pending["isbn13"] | "";
          loaded.pending_.coverSha256 = pending["coverSha256"] | "";
          loaded.pending_.coverMime = pending["coverMime"] | "";
          parsed = loaded.pending_.sequence < loaded.nextSequence_ ||
                   (loaded.pending_.sequence == UINT32_MAX && loaded.nextSequence_ == UINT32_MAX);
        }
      }
    }

    if (parsed && !root["cover"].isNull()) {
      parsed = root["cover"].is<JsonObjectConst>();
      if (parsed) {
        const JsonObjectConst cover = root["cover"].as<JsonObjectConst>();
        parsed = isRequiredString(cover, "bookId") && isRequiredString(cover, "sha256") &&
                 isRequiredString(cover, "mime") && isRequiredString(cover, "path");
        if (parsed) {
          loaded.hasCover_ = true;
          loaded.cover_.bookId = cover["bookId"].as<const char*>();
          loaded.cover_.sha256 = cover["sha256"].as<const char*>();
          loaded.cover_.mime = cover["mime"].as<const char*>();
          loaded.cover_.path = cover["path"].as<const char*>();
        }
      }
    }

    if (parsed) {
      parsed = isReadingSyncQueueStateValid(loaded.terminal_, loaded.hasPending_, loaded.hasCover_);
    }
  }

  if (!parsed) {
    preserveCorruptQueue();
    corrupt_ = true;
    return false;
  }

  *this = std::move(loaded);
  return true;
}

bool ReadingSyncQueue::enqueue(ReadingSyncMetadata metadata, const ReadingCoverJob* cover) {
  if (corrupt_ || nextSequence_ == 0 || (terminal_ && terminalReason_ == "sequence_exhausted")) {
    return false;
  }
  if (metadata.schemaVersion != kSchemaVersion || metadata.bookId.empty() || metadata.title.empty() ||
      metadata.progressPercent > 100) {
    return false;
  }

  const std::string nextFingerprint = makeReadingFingerprint(metadata);
  if (hasPending_ && nextFingerprint == makeReadingFingerprint(pending_)) {
    return true;
  }
  if (nextFingerprint == lastAcceptedFingerprint_) {
    return true;
  }
  if (nextSequence_ == UINT32_MAX && hasPending_) {
    return false;
  }

  ReadingSyncQueue previous = *this;
  metadata.sequence = nextSequence_;
  if (nextSequence_ < UINT32_MAX) {
    ++nextSequence_;
  }
  pending_ = std::move(metadata);
  hasPending_ = true;
  terminal_ = false;
  terminalReason_.clear();
  if (cover != nullptr) {
    cover_ = *cover;
    hasCover_ = true;
  }

  if (saveAtomic()) {
    return true;
  }
  *this = std::move(previous);
  return false;
}

const ReadingSyncMetadata* ReadingSyncQueue::pending() const {
  return !terminal_ && !corrupt_ && hasPending_ ? &pending_ : nullptr;
}

const ReadingCoverJob* ReadingSyncQueue::coverPending() const {
  return !terminal_ && !corrupt_ && hasCover_ ? &cover_ : nullptr;
}

bool ReadingSyncQueue::applyServerResult(const uint32_t requestSequence, const uint32_t lastAcceptedSequence,
                                         const ReadingSyncServerStatus status, const bool keepCover) {
  if (!hasPending_ || pending_.sequence != requestSequence || status == ReadingSyncServerStatus::Unknown) {
    return false;
  }

  ReadingSyncQueue previous = *this;
  const uint32_t advanced = advanceReadingSequence(nextSequence_, lastAcceptedSequence);
  if (advanced == 0) {
    terminal_ = true;
    terminalReason_ = "sequence_exhausted";
    hasPending_ = false;
    pending_ = {};
    hasCover_ = false;
    cover_ = {};
    if (saveAtomic()) {
      return true;
    }
    *this = std::move(previous);
    return false;
  }

  nextSequence_ = advanced;
  if (status == ReadingSyncServerStatus::Accepted || status == ReadingSyncServerStatus::Duplicate) {
    lastAcceptedFingerprint_ = makeReadingFingerprint(pending_);
  }
  hasPending_ = false;
  pending_ = {};
  terminal_ = false;
  terminalReason_.clear();
  if (!keepCover) {
    hasCover_ = false;
    cover_ = {};
  }

  if (saveAtomic()) {
    return true;
  }
  *this = std::move(previous);
  return false;
}

bool ReadingSyncQueue::dropTerminal(const uint32_t requestSequence, const std::string& reason) {
  if (!hasPending_ || pending_.sequence != requestSequence || !isHttpTerminalReason(reason)) {
    return false;
  }

  ReadingSyncQueue previous = *this;
  terminal_ = true;
  terminalReason_ = reason;
  hasPending_ = false;
  pending_ = {};
  hasCover_ = false;
  cover_ = {};
  if (saveAtomic()) {
    return true;
  }
  *this = std::move(previous);
  return false;
}

void ReadingSyncQueue::pauseAuthentication() {
  if (authPaused_) {
    return;
  }
  authPaused_ = true;
  if (!saveAtomic()) {
    authPaused_ = false;
  }
}

void ReadingSyncQueue::resumeAuthentication() {
  if (!authPaused_) {
    return;
  }
  authPaused_ = false;
  if (!saveAtomic()) {
    authPaused_ = true;
  }
}

bool ReadingSyncQueue::authenticationPaused() const { return authPaused_; }

bool ReadingSyncQueue::isCorrupt() const { return corrupt_; }

bool ReadingSyncQueue::saveAtomic() const {
  if (corrupt_) {
    return false;
  }

  // StaticJsonDocument<N> is a dynamically allocated compatibility wrapper in
  // ArduinoJson 7. This is intentionally limited to the cold queue persistence
  // path; actual serialized output is measured and capped at 8 KiB below.
  StaticJsonDocument<MAX_QUEUE_BYTES> document;
  document["schemaVersion"] = kSchemaVersion;
  document["nextSequence"] = nextSequence_;
  document["lastAcceptedFingerprint"] = lastAcceptedFingerprint_.c_str();
  document["authPaused"] = authPaused_;
  document["terminal"] = terminal_;
  document["terminalReason"] = terminalReason_.c_str();

  if (hasPending_) {
    JsonObject pending = document["pending"].to<JsonObject>();
    pending["schemaVersion"] = pending_.schemaVersion;
    pending["sequence"] = pending_.sequence;
    pending["bookId"] = pending_.bookId.c_str();
    pending["title"] = pending_.title.c_str();
    pending["author"] = pending_.author.c_str();
    pending["progressPercent"] = pending_.progressPercent;
    pending["lastReadAt"] = pending_.lastReadAt.c_str();
    pending["isbn13"] = pending_.isbn13.c_str();
    pending["coverSha256"] = pending_.coverSha256.c_str();
    pending["coverMime"] = pending_.coverMime.c_str();
  }
  if (hasCover_) {
    JsonObject cover = document["cover"].to<JsonObject>();
    cover["bookId"] = cover_.bookId.c_str();
    cover["sha256"] = cover_.sha256.c_str();
    cover["mime"] = cover_.mime.c_str();
    cover["path"] = cover_.path.c_str();
  }

  const size_t serializedSize = measureJson(document);
  if (document.overflowed() || serializedSize == 0 || serializedSize > MAX_QUEUE_BYTES) {
    LOG_ERR("RSQ", "Queue JSON exceeds the 8 KiB persistence limit");
    return false;
  }

  Storage.mkdir(QUEUE_DIRECTORY);
  Storage.mkdir(COVER_DIRECTORY);
  if (Storage.exists(QUEUE_TEMP_PATH)) {
    Storage.remove(QUEUE_TEMP_PATH);
  }

  bool wroteCompleteFile = false;
  {
    HalFile file;
    if (!Storage.openFileForWrite("RSQ", QUEUE_TEMP_PATH, file)) {
      return false;
    }
    const size_t written = serializeJson(document, file);
    file.flush();
    wroteCompleteFile = written == serializedSize;
  }
  if (!wroteCompleteFile) {
    Storage.remove(QUEUE_TEMP_PATH);
    LOG_ERR("RSQ", "Queue JSON write was incomplete");
    return false;
  }

  if (Storage.exists(QUEUE_PATH) && !Storage.remove(QUEUE_PATH)) {
    Storage.remove(QUEUE_TEMP_PATH);
    LOG_ERR("RSQ", "Could not replace queue JSON");
    return false;
  }
  if (!Storage.rename(QUEUE_TEMP_PATH, QUEUE_PATH)) {
    Storage.remove(QUEUE_TEMP_PATH);
    LOG_ERR("RSQ", "Could not activate queue JSON");
    return false;
  }
  return true;
}
