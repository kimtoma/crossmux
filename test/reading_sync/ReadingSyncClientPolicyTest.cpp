#include <gtest/gtest.h>

#include <climits>

#include "reading_sync/ReadingSyncQueue.h"
#include "reading_sync/ReadingSyncResponseValidation.h"

namespace {
ReadingCoverJob makeValidCoverJob() {
  const std::string sha(64, 'a');
  return {"book-42", sha, "image/jpeg", "/.crosspoint/reading_sync/covers/" + sha + ".jpg"};
}
}  // namespace

TEST(ReadingSyncQueueCoverJob, RequiresCanonicalHashMimeAndPath) {
  ReadingCoverJob cover = makeValidCoverJob();
  EXPECT_TRUE(isReadingCoverJobValid(cover));

  cover.sha256[0] = 'A';
  EXPECT_FALSE(isReadingCoverJobValid(cover));
  cover = makeValidCoverJob();
  cover.mime = "image/png";
  EXPECT_FALSE(isReadingCoverJobValid(cover));
  cover = makeValidCoverJob();
  cover.path = "/tmp/" + cover.sha256 + ".jpg";
  EXPECT_FALSE(isReadingCoverJobValid(cover));
  cover = makeValidCoverJob();
  cover.path += ".bak";
  EXPECT_FALSE(isReadingCoverJobValid(cover));
  cover = makeValidCoverJob();
  cover.bookId = std::string(129, 'b');
  EXPECT_FALSE(isReadingCoverJobValid(cover));
}

TEST(ReadingSyncQueueCoverJob, MatchesOnlyValidatedBookAndHash) {
  const ReadingCoverJob cover = makeValidCoverJob();
  EXPECT_TRUE(matchesReadingCoverJob(cover, cover.bookId, cover.sha256));
  EXPECT_FALSE(matchesReadingCoverJob(cover, "another-book", cover.sha256));
  EXPECT_FALSE(matchesReadingCoverJob(cover, cover.bookId, std::string(64, 'b')));

  ReadingCoverJob unsafe = cover;
  unsafe.path = "/books/private.epub";
  EXPECT_FALSE(matchesReadingCoverJob(unsafe, unsafe.bookId, unsafe.sha256));
}

TEST(ReadingSyncResponseValidation, RequiresEveryFieldAndMatchingSequence) {
  ReadingSyncWireResponse wire{true, "accepted", true, 42, true, 42, true, true};
  ReadingSyncResponse response;
  EXPECT_TRUE(validateReadingSyncResponse(wire, 42, response));
  EXPECT_EQ(ReadingSyncServerStatus::Accepted, response.status);
  EXPECT_EQ(42u, response.sequence);
  EXPECT_EQ(42u, response.lastAcceptedSequence);
  EXPECT_TRUE(response.coverRequired);

  EXPECT_FALSE(validateReadingSyncResponse(wire, 41, response));

  wire.hasLastAcceptedSequence = false;
  EXPECT_FALSE(validateReadingSyncResponse(wire, 42, response));
  wire.hasLastAcceptedSequence = true;
  wire.hasStatus = false;
  EXPECT_FALSE(validateReadingSyncResponse(wire, 42, response));
  wire.hasStatus = true;
  wire.hasSequence = false;
  EXPECT_FALSE(validateReadingSyncResponse(wire, 42, response));
  wire.hasSequence = true;
  wire.hasCoverRequired = false;
  EXPECT_FALSE(validateReadingSyncResponse(wire, 42, response));
}

TEST(ReadingSyncResponseValidation, EnforcesDuplicateAndStaleSemantics) {
  ReadingSyncResponse response;
  ReadingSyncWireResponse duplicate{true, "duplicate", true, 42, true, 42, true, false};
  EXPECT_TRUE(validateReadingSyncResponse(duplicate, 42, response));
  EXPECT_EQ(ReadingSyncServerStatus::Duplicate, response.status);

  ReadingSyncWireResponse stale{true, "stale", true, 41, true, 42, true, false};
  EXPECT_TRUE(validateReadingSyncResponse(stale, 41, response));
  EXPECT_EQ(ReadingSyncServerStatus::Stale, response.status);

  stale.coverRequired = true;
  EXPECT_FALSE(validateReadingSyncResponse(stale, 41, response));
  stale.coverRequired = false;
  stale.lastAcceptedSequence = stale.sequence;
  EXPECT_FALSE(validateReadingSyncResponse(stale, 41, response));
  stale.lastAcceptedSequence = 42;
  stale.status = "other";
  EXPECT_FALSE(validateReadingSyncResponse(stale, 41, response));
}

TEST(ReadingSyncResponseValidation, RejectsOutOfRangeSequencesAndInvalidAcceptedSemantics) {
  ReadingSyncResponse response;
  ReadingSyncWireResponse accepted{true, "accepted", true, 0, true, 0, true, false};
  EXPECT_FALSE(validateReadingSyncResponse(accepted, 0, response));

  accepted.sequence = static_cast<uint64_t>(UINT32_MAX) + 1u;
  accepted.lastAcceptedSequence = accepted.sequence;
  EXPECT_FALSE(validateReadingSyncResponse(accepted, UINT32_MAX, response));

  accepted.sequence = 42;
  accepted.lastAcceptedSequence = static_cast<uint64_t>(UINT32_MAX) + 1u;
  EXPECT_FALSE(validateReadingSyncResponse(accepted, 42, response));

  accepted.lastAcceptedSequence = 41;
  EXPECT_FALSE(validateReadingSyncResponse(accepted, 42, response));
}

TEST(ReadingSyncResponseValidation, DoesNotMutateOutputOnFailure) {
  ReadingSyncResponse response{ReadingSyncServerStatus::Duplicate, 7, 9, true};
  const ReadingSyncWireResponse invalid{true, "unknown", true, 42, true, 42, true, false};

  EXPECT_FALSE(validateReadingSyncResponse(invalid, 42, response));
  EXPECT_EQ(ReadingSyncServerStatus::Duplicate, response.status);
  EXPECT_EQ(7u, response.sequence);
  EXPECT_EQ(9u, response.lastAcceptedSequence);
  EXPECT_TRUE(response.coverRequired);
}

TEST(ReadingSyncMetadataValidation, EnforcesWorkerStringAndSequenceBounds) {
  ReadingSyncMetadata metadata;
  metadata.schemaVersion = 1;
  metadata.sequence = 42;
  metadata.bookId = "book-42";
  metadata.title = std::string(300, 't');
  metadata.author = std::string(200, 'a');
  metadata.progressPercent = 100;
  EXPECT_TRUE(isReadingSyncMetadataBounded(metadata, true));

  metadata.sequence = 0;
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));
  EXPECT_TRUE(isReadingSyncMetadataBounded(metadata, false));
  metadata.sequence = 42;

  metadata.bookId = std::string(129, 'b');
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));
  metadata.bookId = std::string("book\x1f", 5);
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));
  metadata.bookId = "book-42";

  metadata.title.push_back('t');
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));
  metadata.title.resize(300);
  metadata.author.push_back('a');
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));
}

TEST(ReadingSyncMetadataValidation, EnforcesOptionalCoverPairContract) {
  ReadingSyncMetadata metadata;
  metadata.schemaVersion = 1;
  metadata.sequence = 42;
  metadata.bookId = "book-42";
  metadata.title = "Title";
  metadata.coverSha256 = std::string(64, 'a');
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));

  metadata.coverMime = "image/jpeg";
  EXPECT_TRUE(isReadingSyncMetadataBounded(metadata, true));
  metadata.coverSha256[0] = 'A';
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));
  metadata.coverSha256[0] = 'a';
  metadata.coverMime = "image/gif";
  EXPECT_FALSE(isReadingSyncMetadataBounded(metadata, true));
}
