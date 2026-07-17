#pragma once

#include <cstdint>
#include <string>

struct ReadingSyncMetadata {
  uint8_t schemaVersion = 1;
  uint32_t sequence = 0;
  std::string bookId;
  std::string title;
  std::string author;
  uint8_t progressPercent = 0;
  std::string lastReadAt;
  std::string isbn13;
  std::string coverSha256;
  std::string coverMime;
};

struct ReadingSyncSessionCandidate {
  uint32_t sessionMs;
  uint8_t startProgressPercent;
  uint8_t endProgressPercent;
  bool completedThisSession;
};

enum class ReadingSyncServerStatus : uint8_t { Unknown, Accepted, Duplicate, Stale };
enum class ReadingSyncDisposition : uint8_t { DeletePending, DeleteTerminal, PauseAuthentication, Retry };
