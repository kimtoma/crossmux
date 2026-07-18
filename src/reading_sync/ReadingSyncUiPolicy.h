#pragma once

#include <cstdint>
#include <string>

#include "ReadingSyncTypes.h"

enum class KimtomaSyncUiState : uint8_t {
  NotConfigured,
  ReadyNoBook,
  Ready,
  Pending,
  Syncing,
  AuthenticationRequired,
  QueueCorrupt,
};

enum class KimtomaConnectionTestState : uint8_t {
  Idle,
  Running,
  Succeeded,
  AuthenticationFailed,
  NetworkFailed,
};

enum class ReadingSyncWorkerOperation : uint8_t { None, Sync, Validate };
enum class ReadingSyncCoverState : uint8_t { None, Pending, Uploaded };

struct KimtomaSyncUiInputs {
  bool configured;
  bool authenticationPaused;
  bool syncing;
  bool pending;
  bool queueCorrupt;
  bool hasAccepted;
};

struct KimtomaTextRowLayout {
  int statusBoxHeight;
  int statusTextOffsetY;
  int actionBoxHeight;
  int actionRowPitch;
  int actionTextOffsetY;
};

struct ReadingSyncAcceptedSummary {
  std::string title;
  std::string author;
  uint8_t progressPercent = 0;
  std::string lastReadAt;
  uint32_t acceptedAt = 0;
  ReadingSyncCoverState coverState = ReadingSyncCoverState::None;
};

KimtomaSyncUiState resolveKimtomaSyncUiState(const KimtomaSyncUiInputs& inputs);
KimtomaTextRowLayout makeKimtomaTextRowLayout(int lineHeight);
bool canStartReadingSyncOperation(ReadingSyncWorkerOperation current, ReadingSyncWorkerOperation requested);
bool isReadingSyncAcceptedSummaryValid(const ReadingSyncAcceptedSummary& summary);
bool shouldReplaceAcceptedSummary(ReadingSyncServerStatus status);
const char* readingSyncCoverStateName(ReadingSyncCoverState state);
bool parseReadingSyncCoverState(const std::string& value, ReadingSyncCoverState& out);
