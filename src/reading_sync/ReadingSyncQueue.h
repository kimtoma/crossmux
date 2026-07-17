#pragma once

#include <cstdint>
#include <string>

#include "ReadingSyncTypes.h"

struct ReadingCoverJob {
  std::string bookId;
  std::string sha256;
  std::string mime;
  std::string path;
};

class ReadingSyncQueue {
 public:
  static constexpr uint8_t kSchemaVersion = 1;

  static ReadingSyncQueue& getInstance();
  bool loadFromFile();
  bool enqueue(ReadingSyncMetadata metadata, const ReadingCoverJob* cover);
  const ReadingSyncMetadata* pending() const;
  const ReadingCoverJob* coverPending() const;
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
  bool authPaused_ = false;
  bool terminal_ = false;
  bool corrupt_ = false;
  std::string lastAcceptedFingerprint_;
  std::string terminalReason_;
  ReadingSyncMetadata pending_;
  ReadingCoverJob cover_;
};

#define READING_SYNC_QUEUE ReadingSyncQueue::getInstance()
