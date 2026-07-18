#include <gtest/gtest.h>

#include "reading_sync/ReadingSyncUiPolicy.h"

namespace {
ReadingSyncAcceptedSummary makeSummary() {
  return {"제목", "저자", 37, "2026-07-18T12:34:56Z", 1784345696, ReadingSyncCoverState::Uploaded};
}
}  // namespace

TEST(KimtomaSyncUiPolicy, ResolvesStateInApprovedPriorityOrder) {
  EXPECT_EQ(KimtomaSyncUiState::QueueCorrupt, resolveKimtomaSyncUiState({true, false, true, true, true, true}));
  EXPECT_EQ(KimtomaSyncUiState::NotConfigured, resolveKimtomaSyncUiState({false, false, true, true, false, true}));
  EXPECT_EQ(KimtomaSyncUiState::AuthenticationRequired,
            resolveKimtomaSyncUiState({true, true, true, true, false, true}));
  EXPECT_EQ(KimtomaSyncUiState::Syncing, resolveKimtomaSyncUiState({true, false, true, true, false, true}));
  EXPECT_EQ(KimtomaSyncUiState::Pending, resolveKimtomaSyncUiState({true, false, false, true, false, true}));
  EXPECT_EQ(KimtomaSyncUiState::ReadyNoBook, resolveKimtomaSyncUiState({true, false, false, false, false, false}));
  EXPECT_EQ(KimtomaSyncUiState::Ready, resolveKimtomaSyncUiState({true, false, false, false, false, true}));
}

TEST(KimtomaSyncLayout, FitsKoreanUiTextInsideStatusAndActionRows) {
  const KimtomaTextRowLayout layout = makeKimtomaTextRowLayout(30);

  EXPECT_EQ(38, layout.statusBoxHeight);
  EXPECT_EQ(4, layout.statusTextOffsetY);
  EXPECT_LE(layout.statusTextOffsetY + 30, layout.statusBoxHeight);
  EXPECT_EQ(40, layout.actionBoxHeight);
  EXPECT_EQ(44, layout.actionRowPitch);
  EXPECT_EQ(5, layout.actionTextOffsetY);
  EXPECT_LE(layout.actionTextOffsetY + 30, layout.actionBoxHeight);
}

TEST(KimtomaSyncUiPolicy, AllowsOnlyOneWorkerOperation) {
  EXPECT_TRUE(canStartReadingSyncOperation(ReadingSyncWorkerOperation::None, ReadingSyncWorkerOperation::Sync));
  EXPECT_TRUE(canStartReadingSyncOperation(ReadingSyncWorkerOperation::None, ReadingSyncWorkerOperation::Validate));
  EXPECT_FALSE(canStartReadingSyncOperation(ReadingSyncWorkerOperation::Sync, ReadingSyncWorkerOperation::Validate));
  EXPECT_FALSE(canStartReadingSyncOperation(ReadingSyncWorkerOperation::Validate, ReadingSyncWorkerOperation::Sync));
  EXPECT_FALSE(canStartReadingSyncOperation(ReadingSyncWorkerOperation::None, ReadingSyncWorkerOperation::None));
}

TEST(ReadingSyncAcceptedSummary, EnforcesDisplayBounds) {
  ReadingSyncAcceptedSummary summary = makeSummary();
  EXPECT_TRUE(isReadingSyncAcceptedSummaryValid(summary));

  summary.title.clear();
  EXPECT_FALSE(isReadingSyncAcceptedSummaryValid(summary));
  summary = makeSummary();
  summary.title = std::string(301, 't');
  EXPECT_FALSE(isReadingSyncAcceptedSummaryValid(summary));
  summary = makeSummary();
  summary.author = std::string(201, 'a');
  EXPECT_FALSE(isReadingSyncAcceptedSummaryValid(summary));
  summary = makeSummary();
  summary.lastReadAt = std::string(65, 'x');
  EXPECT_FALSE(isReadingSyncAcceptedSummaryValid(summary));
  summary = makeSummary();
  summary.progressPercent = 101;
  EXPECT_FALSE(isReadingSyncAcceptedSummaryValid(summary));
}

TEST(ReadingSyncAcceptedSummary, ReplacesOnlyForAcceptedAndDuplicate) {
  EXPECT_TRUE(shouldReplaceAcceptedSummary(ReadingSyncServerStatus::Accepted));
  EXPECT_TRUE(shouldReplaceAcceptedSummary(ReadingSyncServerStatus::Duplicate));
  EXPECT_FALSE(shouldReplaceAcceptedSummary(ReadingSyncServerStatus::Stale));
  EXPECT_FALSE(shouldReplaceAcceptedSummary(ReadingSyncServerStatus::Unknown));
}

TEST(ReadingSyncAcceptedSummary, ConvertsCoverStateWithoutFallbackStrings) {
  EXPECT_STREQ("none", readingSyncCoverStateName(ReadingSyncCoverState::None));
  EXPECT_STREQ("pending", readingSyncCoverStateName(ReadingSyncCoverState::Pending));
  EXPECT_STREQ("uploaded", readingSyncCoverStateName(ReadingSyncCoverState::Uploaded));

  ReadingSyncCoverState parsed = ReadingSyncCoverState::None;
  EXPECT_TRUE(parseReadingSyncCoverState("pending", parsed));
  EXPECT_EQ(ReadingSyncCoverState::Pending, parsed);
  EXPECT_FALSE(parseReadingSyncCoverState("unknown", parsed));
}
