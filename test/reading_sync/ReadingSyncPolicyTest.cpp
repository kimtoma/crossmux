#include <gtest/gtest.h>

#include <atomic>
#include <type_traits>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#include "ReadingStatsStore.h"
#pragma GCC diagnostic pop
#include "reading_sync/ReadingSyncClient.h"
#include "reading_sync/ReadingSyncCoordinator.h"
#include "reading_sync/ReadingSyncCredentialStore.h"
#include "reading_sync/ReadingSyncPolicy.h"
#include "reading_sync/ReadingSyncQueue.h"
#include "network/ReadingSyncRawRequestState.h"

static_assert(ReadingSyncQueue::kSchemaVersion == 1);
static_assert(ReadingSyncCoordinator::kWifiTimeoutMs == 8000);
static_assert(ReadingSyncCoordinator::kHttpTimeoutMs == 15000);
static_assert(!std::is_copy_constructible_v<ReadingSyncCoordinator>);
static_assert(!std::is_copy_assignable_v<ReadingSyncCoordinator>);
static_assert(
    std::is_invocable_r_v<void, decltype(&ReadingSyncCoordinator::waitUntilStopped), ReadingSyncCoordinator&>);
static_assert(std::is_invocable_r_v<bool, decltype(&ReadingStatsStore::endSession), ReadingStatsStore&>);
static_assert(std::is_invocable_r_v<void, decltype(&ReadingSyncClient::performPendingSync), ReadingSyncClient&,
                                    ReadingSyncQueue&, ReadingSyncCredentialStore&, const std::atomic_bool*>);
static_assert(ReadingSyncRawRequestState::kTokenBodyCapacity == 256);
static_assert(sizeof(ReadingSyncRawRequestState::TokenBodyBuffer) == 257);

TEST(ReadingSyncRawRequestState, CapturesBoundedChunksAndCompletes) {
  ReadingSyncRawRequestState state;
  constexpr uint8_t first[] = {'{', '"', 't'};
  constexpr uint8_t second[] = {'o', 'k', 'e', 'n', '"', ':', '"', 'x', '"', '}'};

  state.start();
  state.append(first, sizeof(first));
  state.append(second, sizeof(second));
  state.finish();

  EXPECT_TRUE(state.complete());
  EXPECT_FALSE(state.overflowed());
  EXPECT_EQ(sizeof(first) + sizeof(second), state.size());
  EXPECT_STREQ("{\"token\":\"x\"}", state.data());
}

TEST(ReadingSyncRawRequestState, MarksOverflowWithoutGrowingBodyStorage) {
  ReadingSyncRawRequestState state;
  std::array<uint8_t, ReadingSyncRawRequestState::kTokenBodyCapacity + 1> oversized = {};

  state.start();
  state.append(oversized.data(), oversized.size());
  state.finish();

  EXPECT_TRUE(state.complete());
  EXPECT_TRUE(state.overflowed());
  EXPECT_EQ(0u, state.size());
  EXPECT_STREQ("", state.data());
}

TEST(ReadingSyncRawRequestState, AbortedRequestClearsCapturedBytes) {
  ReadingSyncRawRequestState state;
  constexpr uint8_t partial[] = {'s', 'e', 'c', 'r', 'e', 't'};

  state.start();
  state.append(partial, sizeof(partial));
  state.abort();

  EXPECT_FALSE(state.complete());
  EXPECT_TRUE(state.overflowed());
  EXPECT_EQ(0u, state.size());
  EXPECT_STREQ("", state.data());
}

TEST(ReadingSyncPolicy, QualifiesAtAnyApprovedThreshold) {
  EXPECT_TRUE(qualifiesForReadingSync({180000, 20, 20, false}));
  EXPECT_TRUE(qualifiesForReadingSync({1000, 20, 21, false}));
  EXPECT_TRUE(qualifiesForReadingSync({1000, 99, 100, true}));
  EXPECT_FALSE(qualifiesForReadingSync({179999, 20, 20, false}));
  EXPECT_FALSE(qualifiesForReadingSync({1000, 21, 20, false}));
}

TEST(ReadingSyncPolicy, FingerprintExcludesTimestamp) {
  ReadingSyncMetadata a{1, 7, "book", "제목", "저자", 37, "2026-07-17T00:00:00Z", "", "", ""};
  ReadingSyncMetadata b = a;
  b.schemaVersion = 2;
  b.sequence = 8;
  b.lastReadAt = "2026-07-17T12:00:00Z";
  EXPECT_EQ(makeReadingFingerprint(a), makeReadingFingerprint(b));
  b.coverSha256 = std::string(64, 'a');
  b.coverMime = "image/jpeg";
  EXPECT_EQ(makeReadingFingerprint(a), makeReadingFingerprint(b));
  b.isbn13 = "9781234567890";
  EXPECT_EQ(makeReadingFingerprint(a), makeReadingFingerprint(b));

  b = a;
  b.bookId = "other-book";
  EXPECT_NE(makeReadingFingerprint(a), makeReadingFingerprint(b));
  b = a;
  b.title = "다른 제목";
  EXPECT_NE(makeReadingFingerprint(a), makeReadingFingerprint(b));
  b = a;
  b.author = "다른 저자";
  EXPECT_NE(makeReadingFingerprint(a), makeReadingFingerprint(b));
  b = a;
  b.progressPercent = 38;
  EXPECT_NE(makeReadingFingerprint(a), makeReadingFingerprint(b));
}

TEST(ReadingSyncPolicy, MapsHttpClassesWithoutDroppingRetryableWork) {
  EXPECT_EQ(ReadingSyncDisposition::DeletePending, classifyReadingSyncResult(200, ReadingSyncServerStatus::Accepted));
  EXPECT_EQ(ReadingSyncDisposition::DeletePending, classifyReadingSyncResult(200, ReadingSyncServerStatus::Stale));
  EXPECT_EQ(ReadingSyncDisposition::DeletePending, classifyReadingSyncResult(200, ReadingSyncServerStatus::Duplicate));
  EXPECT_EQ(ReadingSyncDisposition::Retry, classifyReadingSyncResult(200, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::PauseAuthentication,
            classifyReadingSyncResult(401, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::PauseAuthentication,
            classifyReadingSyncResult(403, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::DeleteTerminal, classifyReadingSyncResult(400, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::DeleteTerminal, classifyReadingSyncResult(413, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::DeleteTerminal, classifyReadingSyncResult(422, ReadingSyncServerStatus::Unknown));
  EXPECT_EQ(ReadingSyncDisposition::Retry, classifyReadingSyncResult(503, ReadingSyncServerStatus::Unknown));
}

TEST(ReadingSyncPolicy, AdvancesPastServerAndDetectsExhaustion) {
  EXPECT_EQ(43u, advanceReadingSequence(8, 42));
  EXPECT_EQ(8u, advanceReadingSequence(8, 7));
  EXPECT_EQ(0u, advanceReadingSequence(UINT32_MAX, UINT32_MAX));
}

TEST(ReadingSyncPolicy, CoalescingKeepsOnlyNewestFingerprint) {
  ReadingSyncMetadata oldValue{1, 10, "book", "제목", "저자", 20, "2026-07-17T00:00:00Z", "", "", ""};
  ReadingSyncMetadata sameValue = oldValue;
  sameValue.sequence = 11;
  sameValue.lastReadAt = "2026-07-17T12:00:00Z";
  EXPECT_EQ(makeReadingFingerprint(oldValue), makeReadingFingerprint(sameValue));
  sameValue.progressPercent = 21;
  EXPECT_NE(makeReadingFingerprint(oldValue), makeReadingFingerprint(sameValue));
}

TEST(ReadingSyncPolicy, TerminalQueueCannotRetainPendingWork) {
  EXPECT_TRUE(isReadingSyncQueueStateValid(false, false, false));
  EXPECT_TRUE(isReadingSyncQueueStateValid(false, true, false));
  EXPECT_TRUE(isReadingSyncQueueStateValid(false, false, true));
  EXPECT_TRUE(isReadingSyncQueueStateValid(false, true, true));
  EXPECT_TRUE(isReadingSyncQueueStateValid(true, false, false));
  EXPECT_FALSE(isReadingSyncQueueStateValid(true, true, false));
  EXPECT_FALSE(isReadingSyncQueueStateValid(true, false, true));
  EXPECT_FALSE(isReadingSyncQueueStateValid(true, true, true));
}
