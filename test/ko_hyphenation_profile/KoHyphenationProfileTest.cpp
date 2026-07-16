#include <gtest/gtest.h>

#include "Epub/Epub/hyphenation/LanguageRegistry.h"

TEST(KoHyphenationProfile, ExcludesGeneratedLanguageDictionaries) {
  EXPECT_EQ(getLanguageHyphenatorForPrimaryTag("en"), nullptr);
  EXPECT_EQ(getLanguageHyphenatorForPrimaryTag("de"), nullptr);
  EXPECT_EQ(getLanguageHyphenatorForPrimaryTag("ru"), nullptr);

  const LanguageEntryView entries = getLanguageEntries();
  EXPECT_EQ(entries.size, 0u);
  EXPECT_NE(entries.data, nullptr);
  EXPECT_EQ(entries.begin(), entries.end());
}
