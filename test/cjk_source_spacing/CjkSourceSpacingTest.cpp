#include <gtest/gtest.h>

#include "Epub/Epub/CjkSourceSpacing.h"

TEST(CjkSourceSpacing, ExplicitSpaceAlwaysSurvives) {
  EXPECT_TRUE(shouldInsertCjkSourceSpace(true, false, U'한', U'글'));
  EXPECT_TRUE(shouldInsertCjkSourceSpace(true, false, U'中', U'文'));
}

TEST(CjkSourceSpacing, KoreanSegmentBoundarySurvives) {
  EXPECT_TRUE(shouldInsertCjkSourceSpace(false, true, U'책', U'읽'));
  EXPECT_TRUE(shouldInsertCjkSourceSpace(false, true, U'A', U'한'));
  EXPECT_TRUE(shouldInsertCjkSourceSpace(false, true, U'A', U'B'));
}

TEST(CjkSourceSpacing, ChineseFormattingNewlineDoesNotBecomeSpace) {
  EXPECT_FALSE(shouldInsertCjkSourceSpace(false, true, U'中', U'文'));
  EXPECT_FALSE(shouldInsertCjkSourceSpace(false, false, U'한', U'글'));
  EXPECT_FALSE(shouldInsertCjkSourceSpace(false, true, 0, U'한'));
}
