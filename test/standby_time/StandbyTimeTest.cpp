#include <gtest/gtest.h>

#include <ctime>

#include "HalClock.h"
#include "StandbyTime.h"

TEST(StandbyTime, UsesAnAlreadyValidSystemClockWithoutAStandbyNtpFlag) {
  ASSERT_GE(std::time(nullptr), static_cast<std::time_t>(1704067200));

  standby_time::setSynced(false);

  EXPECT_TRUE(standby_time::isSynced());
}

TEST(StandbyTime, AppliesKoreaOffsetEastOfUtcForLocalTime) {
  constexpr uint8_t kKoreaUtcOffsetQuarterHoursBiased = 84;
  constexpr time_t kUnixEpoch = 0;

  halClock.applySavedTimezone(kKoreaUtcOffsetQuarterHoursBiased);
  tm local{};
  EXPECT_NE(localtime_r(&kUnixEpoch, &local), nullptr);
  EXPECT_EQ(local.tm_hour, 9);

  halClock.applySavedTimezone(48);  // Restore UTC for the remaining host tests.
}
