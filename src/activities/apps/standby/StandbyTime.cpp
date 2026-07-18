#include "StandbyTime.h"

#include <Arduino.h>
#include <time.h>

namespace standby_time {
namespace {

constexpr time_t kValidSystemClockThreshold = 1704067200;  // 2024-01-01 UTC

bool g_synced =
#ifdef CROSSPOINT_EMULATED
    true;  // Host system time is already correct; skip Wi-Fi/NTP entirely.
#else
    false;
#endif

// Pre-sync display: start at a plausible wall-clock time and tick forward so
// the clock looks alive instead of stuck at 00:00 during the few seconds
// before NTP returns (or indefinitely when Wi-Fi sync is unavailable).
constexpr unsigned kFallbackStartHH = 16;
constexpr unsigned kFallbackStartMM = 38;

}  // namespace

bool isSynced() { return g_synced || time(nullptr) >= kValidSystemClockThreshold; }

void setSynced(bool v) { g_synced = v; }

void getNowHHMM(uint32_t fallbackStartMs, unsigned& hh, unsigned& mm) {
  if (isSynced()) {
    const time_t now = time(nullptr);
    struct tm tmLocal;
    localtime_r(&now, &tmLocal);
    hh = static_cast<unsigned>(tmLocal.tm_hour);
    mm = static_cast<unsigned>(tmLocal.tm_min);
  } else {
    const uint32_t elapsedMin = (millis() - fallbackStartMs) / 60000u;
    const uint32_t totalMin = kFallbackStartHH * 60u + kFallbackStartMM + elapsedMin;
    hh = (totalMin / 60u) % 24u;
    mm = totalMin % 60u;
  }
}

uint32_t getMinuteTick(uint32_t fallbackStartMs) {
  if (isSynced()) {
    const time_t now = time(nullptr);
    return static_cast<uint32_t>(now / 60);
  }
  return (millis() - fallbackStartMs) / 60000u;
}

}  // namespace standby_time
