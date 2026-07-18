#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "ReadingSyncResponseValidation.h"
#include "ReadingSyncUiPolicy.h"

struct ReadingCoverJob {
  std::string bookId;
  std::string sha256;
  std::string mime;
  std::string path;
};

enum class ReadingSyncFingerprintState : uint8_t { New, Pending, Accepted };
enum class ReadingSyncCoverAction : uint8_t { Preserve, MergeIntoPending, RegisterCoverOnly, Replace };

inline ReadingSyncCoverAction classifyReadingSyncCoverAction(const ReadingSyncFingerprintState fingerprintState,
                                                             const bool hasIncomingCover) {
  if (!hasIncomingCover) {
    return ReadingSyncCoverAction::Preserve;
  }
  switch (fingerprintState) {
    case ReadingSyncFingerprintState::New:
      return ReadingSyncCoverAction::Replace;
    case ReadingSyncFingerprintState::Pending:
      return ReadingSyncCoverAction::MergeIntoPending;
    case ReadingSyncFingerprintState::Accepted:
      return ReadingSyncCoverAction::RegisterCoverOnly;
  }
  return ReadingSyncCoverAction::Preserve;
}

inline bool isReadingCoverJobValid(const ReadingCoverJob& cover) {
  static constexpr char COVER_DIRECTORY[] = "/.crosspoint/reading_sync/covers/";
  static constexpr size_t SHA256_HEX_SIZE = 64;
  static constexpr size_t EXTENSION_SIZE = 4;

  if (!isReadingSyncBookIdBounded(cover.bookId) || cover.sha256.size() != SHA256_HEX_SIZE) {
    return false;
  }
  const bool lowercaseHex = std::all_of(cover.sha256.begin(), cover.sha256.end(), [](const char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  });
  if (!lowercaseHex) {
    return false;
  }

  const char* extension = nullptr;
  if (cover.mime == "image/jpeg") {
    extension = ".jpg";
  } else if (cover.mime == "image/png") {
    extension = ".png";
  } else {
    return false;
  }

  constexpr size_t directorySize = sizeof(COVER_DIRECTORY) - 1;
  return cover.path.size() == directorySize + SHA256_HEX_SIZE + EXTENSION_SIZE &&
         cover.path.compare(0, directorySize, COVER_DIRECTORY) == 0 &&
         cover.path.compare(directorySize, SHA256_HEX_SIZE, cover.sha256) == 0 &&
         cover.path.compare(directorySize + SHA256_HEX_SIZE, EXTENSION_SIZE, extension) == 0;
}

inline bool matchesReadingCoverJob(const ReadingCoverJob& cover, const std::string& bookId, const std::string& sha256) {
  return isReadingCoverJobValid(cover) && cover.bookId == bookId && cover.sha256 == sha256;
}

class ReadingSyncQueue {
 public:
  static constexpr uint8_t kSchemaVersion = 2;

  static ReadingSyncQueue& getInstance();
  bool loadFromFile();
  bool enqueue(ReadingSyncMetadata metadata, const ReadingCoverJob* cover);
  const ReadingSyncMetadata* pending() const;
  const ReadingCoverJob* coverPending() const;
  const ReadingSyncAcceptedSummary* lastAccepted() const;
  bool clearCoverJob(const std::string& bookId, const std::string& sha256);
  bool applyServerResult(uint32_t requestSequence, uint32_t lastAcceptedSequence, ReadingSyncServerStatus status,
                         bool keepCover);
  bool dropTerminal(uint32_t requestSequence, const std::string& reason);
  void pauseAuthentication();
  void resumeAuthentication();
  bool authenticationPaused() const;
  bool isCorrupt() const;

 private:
  bool saveAtomic() const;

  uint32_t nextSequence_ = 1;
  bool hasPending_ = false;
  bool hasCover_ = false;
  bool hasLastAccepted_ = false;
  bool authPaused_ = false;
  bool terminal_ = false;
  bool corrupt_ = false;
  std::string lastAcceptedFingerprint_;
  std::string terminalReason_;
  ReadingSyncMetadata pending_;
  ReadingCoverJob cover_;
  ReadingSyncAcceptedSummary lastAccepted_;
};

#define READING_SYNC_QUEUE ReadingSyncQueue::getInstance()
