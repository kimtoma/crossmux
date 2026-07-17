#include "ReadingSyncCoordinator.h"

#ifdef ENABLE_KIMTOMA_READING_SYNC

#include <Arduino.h>
#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <array>
#include <ctime>
#include <string>
#include <utility>

#ifndef CROSSPOINT_EMULATED
#include <mbedtls/sha256.h>
#endif

#include "EpubOriginalCoverSource.h"
#include "ReadingStatsStore.h"
#include "ReadingSyncClient.h"
#include "ReadingSyncCredentialStore.h"
#include "ReadingSyncPolicy.h"
#include "ReadingSyncQueue.h"
#include "util/BookIdentity.h"
#include "util/TimeUtils.h"

namespace {
constexpr size_t SHA256_BYTES = 32;
constexpr size_t SHA256_HEX_CHARS = SHA256_BYTES * 2;
constexpr size_t RFC3339_UTC_CHARS = 20;
constexpr uint32_t READING_SYNC_TASK_STACK_BYTES = 4096;

std::string digestToLowerHex(const std::array<uint8_t, SHA256_BYTES>& digest) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  // This bounded identifier becomes queue-owned state. Reserve its exact size
  // once so the cold enqueue path does not repeatedly grow the allocation.
  std::string result;
  result.reserve(SHA256_HEX_CHARS);
  for (const uint8_t byte : digest) {
    result.push_back(HEX_DIGITS[byte >> 4]);
    result.push_back(HEX_DIGITS[byte & 0x0f]);
  }
  return result;
}

std::string publicBookId(std::string resolvedBookId) {
  if (!BookIdentity::isLegacyBookId(resolvedBookId)) {
    return resolvedBookId;
  }

#ifdef CROSSPOINT_EMULATED
  return {};
#else
  std::array<uint8_t, SHA256_BYTES> digest = {};
  if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(resolvedBookId.data()), resolvedBookId.size(),
                     digest.data(), 0) != 0) {
    return {};
  }
  return digestToLowerHex(digest);
#endif
}

std::string formatUtcRfc3339(const uint32_t epochSeconds) {
  if (!TimeUtils::isClockValid(epochSeconds)) {
    return {};
  }

  const time_t rawTime = static_cast<time_t>(epochSeconds);
  struct tm utcTime = {};
  if (gmtime_r(&rawTime, &utcTime) == nullptr) {
    return {};
  }

  char formatted[RFC3339_UTC_CHARS + 1] = {};
  if (std::strftime(formatted, sizeof(formatted), "%Y-%m-%dT%H:%M:%SZ", &utcTime) != RFC3339_UTC_CHARS) {
    return {};
  }
  return formatted;
}

bool queueReferencesCoverPath(const std::string& path) {
  const ReadingCoverJob* pendingCover = READING_SYNC_QUEUE.coverPending();
  return pendingCover != nullptr && pendingCover->path == path;
}

void removeUnreferencedCover(const std::string& path) {
  if (path.empty() || queueReferencesCoverPath(path) || !Storage.exists(path.c_str())) {
    return;
  }
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("RSY", "Could not remove an unreferenced staged cover");
  }
}
}  // namespace

ReadingSyncCoordinator& ReadingSyncCoordinator::getInstance() {
  static ReadingSyncCoordinator instance;
  return instance;
}

bool ReadingSyncCoordinator::loadFromFile() {
  const bool queueLoaded = READING_SYNC_QUEUE.loadFromFile();
  const bool credentialsLoaded = READING_SYNC_CREDENTIALS.loadFromFile();
  return queueLoaded && credentialsLoaded;
}

bool ReadingSyncCoordinator::enqueueAfterSession(const ReadingSessionSnapshot& snapshot, const ReadingBookStats& book,
                                                 const Epub& epub) {
  if (!snapshot.valid || snapshot.bookId.empty() || snapshot.bookId != book.bookId ||
      !qualifiesForReadingSync({snapshot.sessionMs, snapshot.startProgressPercent, snapshot.endProgressPercent,
                                snapshot.completedThisSession})) {
    return false;
  }

  std::string bookId = publicBookId(BookIdentity::resolveStableBookId(book.path));
  if (bookId.empty()) {
    return false;
  }

  ReadingSyncMetadata metadata;
  metadata.schemaVersion = ReadingSyncQueue::kSchemaVersion;
  metadata.bookId = std::move(bookId);
  metadata.title = book.title;
  metadata.author = book.author;
  metadata.progressPercent = snapshot.endProgressPercent;
  metadata.lastReadAt = formatUtcRfc3339(book.lastReadAt);

  ReadingCoverJob stagedCover;
  bool createdNewCoverFile = false;
  const bool hasStagedCover = stageOriginalEpubCover(epub, metadata.bookId, stagedCover, &createdNewCoverFile);
  if (hasStagedCover) {
    metadata.coverSha256 = stagedCover.sha256;
    metadata.coverMime = stagedCover.mime;
  }

  std::string previousCoverPath;
  if (const ReadingCoverJob* previousCover = READING_SYNC_QUEUE.coverPending(); previousCover != nullptr) {
    // The canonical path is bounded by isReadingCoverJobValid(). A temporary
    // copy is required so replacement cleanup happens only after queue.json is durable.
    previousCoverPath = previousCover->path;
  }

  const bool enqueued = READING_SYNC_QUEUE.enqueue(metadata, hasStagedCover ? &stagedCover : nullptr);
  if (!enqueued) {
    if (createdNewCoverFile) {
      removeUnreferencedCover(stagedCover.path);
    }
    return false;
  }

  if (createdNewCoverFile) {
    removeUnreferencedCover(stagedCover.path);
  }
  if (!previousCoverPath.empty() && previousCoverPath != stagedCover.path) {
    removeUnreferencedCover(previousCoverPath);
  }
  return true;
}

void ReadingSyncCoordinator::startOneShotIfPending() {
  manualRetryRequested_.exchange(false, std::memory_order_relaxed);
  if (READING_SYNC_QUEUE.isCorrupt() || READING_SYNC_QUEUE.authenticationPaused() ||
      !READING_SYNC_CREDENTIALS.hasToken() ||
      (READING_SYNC_QUEUE.pending() == nullptr && READING_SYNC_QUEUE.coverPending() == nullptr)) {
    return;
  }

  bool expected = false;
  if (!running_.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
    return;
  }
  cancelRequested_.store(false, std::memory_order_relaxed);

  const BaseType_t created =
      xTaskCreate(&ReadingSyncCoordinator::taskEntry, "ReadingSync", READING_SYNC_TASK_STACK_BYTES, this, 1, nullptr);
  if (created != pdPASS) {
    running_.store(false, std::memory_order_relaxed);
    LOG_ERR("RSY", "Could not create reading sync task");
  }
}

void ReadingSyncCoordinator::requestManualRetry() { manualRetryRequested_.store(true, std::memory_order_relaxed); }

void ReadingSyncCoordinator::requestCancel() { cancelRequested_.store(true, std::memory_order_relaxed); }

bool ReadingSyncCoordinator::isRunning() const { return running_.load(std::memory_order_relaxed); }

void ReadingSyncCoordinator::taskEntry(void* context) {
  auto* coordinator = static_cast<ReadingSyncCoordinator*>(context);
  READING_SYNC_CLIENT.performPendingSync(READING_SYNC_QUEUE, READING_SYNC_CREDENTIALS, &coordinator->cancelRequested_);
  coordinator->running_.store(false, std::memory_order_relaxed);
  vTaskDelete(nullptr);
}

#endif
