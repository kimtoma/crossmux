#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>

#include "ReadingSyncQueue.h"

class Epub;

inline bool isReadingCoverSizeAllowed(const size_t size) { return size >= 1 && size <= 2097152; }

inline bool isReadingCoverPersistenceComplete(const size_t streamedSize, const size_t persistedSize,
                                              const bool closeSucceeded) {
  return closeSucceeded && streamedSize == persistedSize;
}

inline std::string detectReadingCoverMime(const uint8_t* prefix, const size_t size) {
  if (size >= 3 && prefix[0] == 0xff && prefix[1] == 0xd8 && prefix[2] == 0xff) {
    return "image/jpeg";
  }
  static constexpr uint8_t png[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
  if (size >= sizeof(png) && std::equal(std::begin(png), std::end(png), prefix)) {
    return "image/png";
  }
  return {};
}

bool stageOriginalEpubCover(const Epub& epub, const std::string& bookId, ReadingCoverJob& out);
