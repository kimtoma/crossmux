#pragma once

#include <atomic>
#include <cstdint>

#include "ReadingSyncUiPolicy.h"

class Epub;
struct ReadingBookStats;
struct ReadingSessionSnapshot;

class ReadingSyncCoordinator {
 public:
  static constexpr uint32_t kWifiTimeoutMs = 8000;
  static constexpr uint32_t kHttpTimeoutMs = 15000;

  static ReadingSyncCoordinator& getInstance();
  bool loadFromFile();
  bool enqueueAfterSession(const ReadingSessionSnapshot& snapshot, const ReadingBookStats& book, const Epub& epub);
  void startOneShotIfPending();
  void requestManualRetry();
  bool requestManualRetryAndStart();
  bool requestConnectionTest();
  void requestCancel();
  void waitUntilStopped() const;
  bool isRunning() const;
  KimtomaConnectionTestState connectionTestState() const;
  ReadingSyncWorkerOperation workerOperation() const;

 private:
  static void taskEntry(void* context);
  bool enqueueLatestBookSnapshot();
  bool startOperation(ReadingSyncWorkerOperation operation, bool requirePending);

  std::atomic_bool cancelRequested_{false};
  std::atomic_bool manualRetryRequested_{false};
  std::atomic_bool running_{false};
  std::atomic<ReadingSyncWorkerOperation> operation_{ReadingSyncWorkerOperation::None};
  std::atomic<KimtomaConnectionTestState> connectionTestState_{KimtomaConnectionTestState::Idle};
};

#define READING_SYNC ReadingSyncCoordinator::getInstance()
