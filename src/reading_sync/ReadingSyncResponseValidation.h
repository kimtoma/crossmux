#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "ReadingSyncTypes.h"

struct ReadingSyncResponse {
  ReadingSyncServerStatus status = ReadingSyncServerStatus::Unknown;
  uint32_t sequence = 0;
  uint32_t lastAcceptedSequence = 0;
  bool coverRequired = false;
};

struct ReadingSyncWireResponse {
  bool hasStatus = false;
  std::string status;
  bool hasSequence = false;
  uint64_t sequence = 0;
  bool hasLastAcceptedSequence = false;
  uint64_t lastAcceptedSequence = 0;
  bool hasCoverRequired = false;
  bool coverRequired = false;
};

bool validateReadingSyncResponse(const ReadingSyncWireResponse& wire, uint32_t expectedSequence,
                                 ReadingSyncResponse& out);
bool isReadingSyncBookIdBounded(const std::string& bookId);
bool isReadingSyncTextBounded(const std::string& value, size_t utf16Limit, bool requireNonWhitespace);
bool isReadingSyncMetadataBounded(const ReadingSyncMetadata& metadata, bool requireSequence);
