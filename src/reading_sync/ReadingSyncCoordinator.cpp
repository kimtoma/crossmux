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
constexpr TickType_t WORKER_WAIT_YIELD_TICKS = 1;

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
  if (!buildReadingSyncMetadataSnapshot(bookId, book.title, book.author, snapshot.endProgressPercent,
                                        formatUtcRfc3339(book.lastReadAt), metadata)) {
    return false;
  }

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

bool ReadingSyncCoordinator::enqueueLatestBookSnapshot() {
  const auto& books = READING_STATS.getBooks();
  if (books.empty()) {
    return false;
  }

  const ReadingBookStats& book = books.front();
  std::string resolvedBookId = book.bookId;
  if (resolvedBookId.empty()) {
    resolvedBookId = BookIdentity::resolveStableBookId(book.path);
  }
  std::string bookId = publicBookId(std::move(resolvedBookId));
  ReadingSyncMetadata metadata;
  if (!buildReadingSyncMetadataSnapshot(bookId, book.title, book.author, book.lastProgressPercent,
                                        formatUtcRfc3339(book.lastReadAt), metadata)) {
    return false;
  }

  // The queue takes one bounded metadata copy for durable ownership. Manual
  // bootstrap intentionally omits cover staging so Wi-Fi/TLS can start without
  // opening an EPUB or allocating an image decoder on this constrained path.
  return READING_SYNC_QUEUE.enqueue(std::move(metadata), nullptr);
}

bool ReadingSyncCoordinator::startOperation(const ReadingSyncWorkerOperation operation, const bool requirePending) {
  if (operation == ReadingSyncWorkerOperation::None || !READING_SYNC_CREDENTIALS.hasToken()) {
    return false;
  }

  bool expected = false;
  // Acquire ownership before inspecting queue or credential state. A prior
  // worker publishes all queue/stats cleanup through its release store.
  if (!running_.compare_exchange_strong(expected, true, std::memory_order_acquire, std::memory_order_relaxed)) {
    return false;
  }

  manualRetryRequested_.exchange(false, std::memory_order_relaxed);
  if (operation == ReadingSyncWorkerOperation::Sync &&
      (READING_SYNC_QUEUE.isCorrupt() || READING_SYNC_QUEUE.authenticationPaused() ||
       (requirePending && READING_SYNC_QUEUE.pending() == nullptr && READING_SYNC_QUEUE.coverPending() == nullptr))) {
    running_.store(false, std::memory_order_release);
    return false;
  }

  cancelRequested_.store(false, std::memory_order_relaxed);
  operation_.store(operation, std::memory_order_release);
  if (operation == ReadingSyncWorkerOperation::Validate) {
    connectionTestState_.store(KimtomaConnectionTestState::Running, std::memory_order_release);
  }

  const BaseType_t created =
      xTaskCreate(&ReadingSyncCoordinator::taskEntry, "ReadingSync", READING_SYNC_TASK_STACK_BYTES, this, 1, nullptr);
  if (created != pdPASS) {
    operation_.store(ReadingSyncWorkerOperation::None, std::memory_order_release);
    if (operation == ReadingSyncWorkerOperation::Validate) {
      connectionTestState_.store(KimtomaConnectionTestState::NetworkFailed, std::memory_order_release);
    }
    running_.store(false, std::memory_order_release);
    LOG_ERR("RSY", "Could not create reading sync task");
    return false;
  }
  return true;
}

void ReadingSyncCoordinator::startOneShotIfPending() {
  const bool hasPending = READING_SYNC_QUEUE.pending() != nullptr;
  const bool hasCover = READING_SYNC_QUEUE.coverPending() != nullptr;
  const bool hasAccepted = READING_SYNC_QUEUE.lastAccepted() != nullptr;
  const bool shouldBootstrap = shouldBootstrapLatestSnapshotForAutomaticSync(hasPending, hasCover, hasAccepted);
  if (shouldBootstrap) {
    enqueueLatestBookSnapshot();
  }
  startOperation(ReadingSyncWorkerOperation::Sync, true);
}

void ReadingSyncCoordinator::requestManualRetry() { manualRetryRequested_.store(true, std::memory_order_relaxed); }

bool ReadingSyncCoordinator::requestManualRetryAndStart() {
  manualRetryRequested_.store(true, std::memory_order_relaxed);
  if (shouldCreateLatestSnapshotForManualSync(READING_SYNC_QUEUE.pending() != nullptr,
                                              READING_SYNC_QUEUE.coverPending() != nullptr) &&
      !enqueueLatestBookSnapshot()) {
    return false;
  }
  return startOperation(ReadingSyncWorkerOperation::Sync, true);
}

bool ReadingSyncCoordinator::requestConnectionTest() {
  return startOperation(ReadingSyncWorkerOperation::Validate, false);
}

void ReadingSyncCoordinator::requestCancel() { cancelRequested_.store(true, std::memory_order_relaxed); }

void ReadingSyncCoordinator::waitUntilStopped() const {
  // The worker releases running_ only after performPendingSync() returns, so
  // observing false also observes NetworkLifecycle's stats reload and cleanup.
  while (running_.load(std::memory_order_acquire)) {
    vTaskDelay(WORKER_WAIT_YIELD_TICKS);
  }
}

bool ReadingSyncCoordinator::isRunning() const { return running_.load(std::memory_order_acquire); }

KimtomaConnectionTestState ReadingSyncCoordinator::connectionTestState() const {
  return connectionTestState_.load(std::memory_order_acquire);
}

ReadingSyncWorkerOperation ReadingSyncCoordinator::workerOperation() const {
  return operation_.load(std::memory_order_acquire);
}

void ReadingSyncCoordinator::taskEntry(void* context) {
  auto* coordinator = static_cast<ReadingSyncCoordinator*>(context);
  const ReadingSyncWorkerOperation operation = coordinator->operation_.load(std::memory_order_acquire);
  switch (operation) {
    case ReadingSyncWorkerOperation::Sync:
      READING_SYNC_CLIENT.performPendingSync(READING_SYNC_QUEUE, READING_SYNC_CREDENTIALS,
                                             &coordinator->cancelRequested_);
      break;
    case ReadingSyncWorkerOperation::Validate: {
      const KimtomaConnectionTestState result =
          READING_SYNC_CLIENT.performValidation(READING_SYNC_CREDENTIALS, &coordinator->cancelRequested_);
      if (result == KimtomaConnectionTestState::Succeeded) {
        READING_SYNC_QUEUE.resumeAuthentication();
      } else if (result == KimtomaConnectionTestState::AuthenticationFailed) {
        READING_SYNC_QUEUE.pauseAuthentication();
      }
      coordinator->connectionTestState_.store(result, std::memory_order_release);
      break;
    }
    case ReadingSyncWorkerOperation::None:
      break;
  }
  coordinator->operation_.store(ReadingSyncWorkerOperation::None, std::memory_order_release);
  coordinator->running_.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}

#endif
