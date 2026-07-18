#include <gtest/gtest.h>

#include "reading_sync/EpubOriginalCoverSource.h"

TEST(EpubCoverPolicy, AcceptsOnlyJpegAndPngMagic) {
  const uint8_t jpg[] = {0xff, 0xd8, 0xff, 0xe0};
  const uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
  const uint8_t bmp[] = {0x42, 0x4d, 0x00, 0x00};
  EXPECT_EQ("image/jpeg", detectReadingCoverMime(jpg, sizeof(jpg)));
  EXPECT_EQ("image/png", detectReadingCoverMime(png, sizeof(png)));
  EXPECT_TRUE(detectReadingCoverMime(bmp, sizeof(bmp)).empty());
}

TEST(EpubCoverPolicy, EnforcesInclusiveTwoMegabyteLimit) {
  EXPECT_FALSE(isReadingCoverSizeAllowed(0));
  EXPECT_TRUE(isReadingCoverSizeAllowed(1));
  EXPECT_TRUE(isReadingCoverSizeAllowed(2097152));
  EXPECT_FALSE(isReadingCoverSizeAllowed(2097153));
}

TEST(EpubCoverPolicy, RequiresPersistedSizeAndSuccessfulClose) {
  EXPECT_TRUE(isReadingCoverPersistenceComplete(1024, 1024, true));
  EXPECT_FALSE(isReadingCoverPersistenceComplete(1024, 1023, true));
  EXPECT_FALSE(isReadingCoverPersistenceComplete(1024, 1024, false));
}
