#pragma once

#include <cstdint>
#include <string>

#include "ReadingSyncTypes.h"

bool qualifiesForReadingSync(const ReadingSyncSessionCandidate& candidate);
std::string makeReadingFingerprint(const ReadingSyncMetadata& metadata);
ReadingSyncDisposition classifyReadingSyncResult(int httpStatus, ReadingSyncServerStatus status);
uint32_t advanceReadingSequence(uint32_t nextSequence, uint32_t lastAcceptedSequence);
