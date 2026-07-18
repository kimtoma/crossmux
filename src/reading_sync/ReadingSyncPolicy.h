#pragma once

#include <cstdint>
#include <string>

#include "ReadingSyncTypes.h"

bool qualifiesForReadingSync(const ReadingSyncSessionCandidate& candidate);
bool isReadingSyncHomeReady(bool firstRenderDone, bool recentsLoaded, bool recentsLoading);
bool shouldCreateLatestSnapshotForManualSync(bool hasPending, bool hasCover);
bool shouldBootstrapLatestSnapshotForAutomaticSync(bool hasPending, bool hasCover, bool hasAccepted);
bool shouldDiscardOrphanedAcceptedFingerprint(bool hasAcceptedSummary, bool fingerprintEmpty);
bool buildReadingSyncMetadataSnapshot(const std::string& bookId, const std::string& title, const std::string& author,
                                      uint8_t progressPercent, const std::string& lastReadAt, ReadingSyncMetadata& out);
std::string makeReadingFingerprint(const ReadingSyncMetadata& metadata);
ReadingSyncDisposition classifyReadingSyncResult(int httpStatus, ReadingSyncServerStatus status);
uint32_t advanceReadingSequence(uint32_t nextSequence, uint32_t lastAcceptedSequence);
bool isReadingSyncQueueStateValid(bool terminal, bool hasPending, bool hasCover);
