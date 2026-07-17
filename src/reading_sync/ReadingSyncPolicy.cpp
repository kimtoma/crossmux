#include "ReadingSyncPolicy.h"

#include <climits>

namespace {
constexpr uint32_t MINIMUM_SYNC_SESSION_MS = 180000u;
constexpr int HTTP_OK = 200;
constexpr int HTTP_BAD_REQUEST = 400;
constexpr int HTTP_UNAUTHORIZED = 401;
constexpr int HTTP_FORBIDDEN = 403;
constexpr int HTTP_CONTENT_TOO_LARGE = 413;
constexpr int HTTP_UNPROCESSABLE_CONTENT = 422;
constexpr char FINGERPRINT_SEPARATOR = '\x1f';
}  // namespace

bool qualifiesForReadingSync(const ReadingSyncSessionCandidate& candidate) {
  const int progressDelta =
      static_cast<int>(candidate.endProgressPercent) - static_cast<int>(candidate.startProgressPercent);
  return candidate.sessionMs >= MINIMUM_SYNC_SESSION_MS || progressDelta >= 1 || candidate.completedThisSession;
}

std::string makeReadingFingerprint(const ReadingSyncMetadata& metadata) {
  const std::string progress = std::to_string(metadata.progressPercent);
  std::string fingerprint;
  fingerprint.reserve(metadata.bookId.size() + metadata.title.size() + metadata.author.size() + progress.size() + 3);
  fingerprint.append(metadata.bookId);
  fingerprint.push_back(FINGERPRINT_SEPARATOR);
  fingerprint.append(metadata.title);
  fingerprint.push_back(FINGERPRINT_SEPARATOR);
  fingerprint.append(metadata.author);
  fingerprint.push_back(FINGERPRINT_SEPARATOR);
  fingerprint.append(progress);
  return fingerprint;
}

ReadingSyncDisposition classifyReadingSyncResult(const int httpStatus, const ReadingSyncServerStatus status) {
  if (httpStatus == HTTP_OK) {
    switch (status) {
      case ReadingSyncServerStatus::Unknown:
        return ReadingSyncDisposition::Retry;
      case ReadingSyncServerStatus::Accepted:
      case ReadingSyncServerStatus::Duplicate:
      case ReadingSyncServerStatus::Stale:
        return ReadingSyncDisposition::DeletePending;
    }
  }

  switch (httpStatus) {
    case HTTP_BAD_REQUEST:
    case HTTP_CONTENT_TOO_LARGE:
    case HTTP_UNPROCESSABLE_CONTENT:
      return ReadingSyncDisposition::DeleteTerminal;
    case HTTP_UNAUTHORIZED:
    case HTTP_FORBIDDEN:
      return ReadingSyncDisposition::PauseAuthentication;
    default:
      return ReadingSyncDisposition::Retry;
  }
}

uint32_t advanceReadingSequence(const uint32_t nextSequence, const uint32_t lastAcceptedSequence) {
  if (lastAcceptedSequence == UINT32_MAX) {
    return 0;
  }

  const uint32_t serverNextSequence = lastAcceptedSequence + 1u;
  return nextSequence > serverNextSequence ? nextSequence : serverNextSequence;
}
