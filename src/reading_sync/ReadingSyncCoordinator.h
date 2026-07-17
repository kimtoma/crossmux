#pragma once

#include <atomic>
#include <cstdint>

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
  void requestCancel();
  void waitUntilStopped() const;
  bool isRunning() const;

 private:
  static void taskEntry(void* context);

  std::atomic_bool cancelRequested_{false};
  std::atomic_bool manualRetryRequested_{false};
  std::atomic_bool running_{false};
};

#define READING_SYNC ReadingSyncCoordinator::getInstance()
