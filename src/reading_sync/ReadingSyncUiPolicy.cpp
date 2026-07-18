#include "ReadingSyncUiPolicy.h"

#include "ReadingSyncResponseValidation.h"

KimtomaSyncUiState resolveKimtomaSyncUiState(const KimtomaSyncUiInputs& inputs) {
  if (inputs.queueCorrupt) {
    return KimtomaSyncUiState::QueueCorrupt;
  }
  if (!inputs.configured) {
    return KimtomaSyncUiState::NotConfigured;
  }
  if (inputs.authenticationPaused) {
    return KimtomaSyncUiState::AuthenticationRequired;
  }
  if (inputs.syncing) {
    return KimtomaSyncUiState::Syncing;
  }
  if (inputs.pending) {
    return KimtomaSyncUiState::Pending;
  }
  return inputs.hasAccepted ? KimtomaSyncUiState::Ready : KimtomaSyncUiState::ReadyNoBook;
}

KimtomaTextRowLayout makeKimtomaTextRowLayout(const int lineHeight) {
  const int boundedLineHeight = lineHeight > 0 ? lineHeight : 1;
  constexpr int statusPaddingY = 4;
  constexpr int actionPaddingY = 5;
  constexpr int actionGap = 4;
  const int actionBoxHeight = boundedLineHeight + actionPaddingY * 2;
  return {boundedLineHeight + statusPaddingY * 2, statusPaddingY, actionBoxHeight, actionBoxHeight + actionGap,
          actionPaddingY};
}

bool canStartReadingSyncOperation(const ReadingSyncWorkerOperation current,
                                  const ReadingSyncWorkerOperation requested) {
  return current == ReadingSyncWorkerOperation::None && requested != ReadingSyncWorkerOperation::None;
}

bool isReadingSyncAcceptedSummaryValid(const ReadingSyncAcceptedSummary& summary) {
  if (!isReadingSyncTextBounded(summary.title, 300, true) || !isReadingSyncTextBounded(summary.author, 200, false) ||
      !isReadingSyncTextBounded(summary.lastReadAt, 64, false) || summary.progressPercent > 100) {
    return false;
  }
  switch (summary.coverState) {
    case ReadingSyncCoverState::None:
    case ReadingSyncCoverState::Pending:
    case ReadingSyncCoverState::Uploaded:
      return true;
  }
  return false;
}

bool shouldReplaceAcceptedSummary(const ReadingSyncServerStatus status) {
  switch (status) {
    case ReadingSyncServerStatus::Accepted:
    case ReadingSyncServerStatus::Duplicate:
      return true;
    case ReadingSyncServerStatus::Stale:
    case ReadingSyncServerStatus::Unknown:
      return false;
  }
  return false;
}

const char* readingSyncCoverStateName(const ReadingSyncCoverState state) {
  switch (state) {
    case ReadingSyncCoverState::None:
      return "none";
    case ReadingSyncCoverState::Pending:
      return "pending";
    case ReadingSyncCoverState::Uploaded:
      return "uploaded";
  }
  return "none";
}

bool parseReadingSyncCoverState(const std::string& value, ReadingSyncCoverState& out) {
  if (value == "none") {
    out = ReadingSyncCoverState::None;
    return true;
  }
  if (value == "pending") {
    out = ReadingSyncCoverState::Pending;
    return true;
  }
  if (value == "uploaded") {
    out = ReadingSyncCoverState::Uploaded;
    return true;
  }
  return false;
}
