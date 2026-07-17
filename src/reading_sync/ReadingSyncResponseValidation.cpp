#include "ReadingSyncResponseValidation.h"

#include <algorithm>
#include <climits>

namespace {
bool utf16LengthWithin(const std::string& value, const size_t limit) {
  size_t utf16Units = 0;
  size_t index = 0;
  while (index < value.size()) {
    const uint8_t lead = static_cast<uint8_t>(value[index]);
    uint32_t codePoint = 0;
    size_t byteCount = 0;
    if (lead <= 0x7f) {
      codePoint = lead;
      byteCount = 1;
    } else if (lead >= 0xc2 && lead <= 0xdf) {
      codePoint = lead & 0x1f;
      byteCount = 2;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      codePoint = lead & 0x0f;
      byteCount = 3;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      codePoint = lead & 0x07;
      byteCount = 4;
    } else {
      return false;
    }
    if (index + byteCount > value.size()) {
      return false;
    }
    for (size_t continuationIndex = 1; continuationIndex < byteCount; ++continuationIndex) {
      const uint8_t continuation = static_cast<uint8_t>(value[index + continuationIndex]);
      if ((continuation & 0xc0) != 0x80) {
        return false;
      }
      codePoint = (codePoint << 6) | (continuation & 0x3f);
    }

    const bool overlong = (byteCount == 2 && codePoint < 0x80) || (byteCount == 3 && codePoint < 0x800) ||
                          (byteCount == 4 && codePoint < 0x10000);
    if (overlong || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
      return false;
    }
    utf16Units += codePoint >= 0x10000 ? 2 : 1;
    if (utf16Units > limit) {
      return false;
    }
    index += byteCount;
  }
  return true;
}

bool hasNonWhitespace(const std::string& value) {
  return std::any_of(value.begin(), value.end(), [](const uint8_t character) {
    return character != ' ' && character != '\t' && character != '\r' && character != '\n' && character != '\f' &&
           character != '\v';
  });
}

bool isBookIdSafe(const std::string& bookId) {
  if (bookId.empty() || !utf16LengthWithin(bookId, 128)) {
    return false;
  }
  const bool controlSafe = std::none_of(bookId.begin(), bookId.end(),
                                        [](const uint8_t character) { return character <= 0x1f || character == 0x7f; });
  if (!controlSafe) {
    return false;
  }
  return bookId.front() != ' ' && bookId.back() != ' ';
}

bool isValidIsbn13(const std::string& isbn13) {
  if (isbn13.size() != 13) {
    return false;
  }
  int sum = 0;
  for (size_t index = 0; index < 12; ++index) {
    const char digit = isbn13[index];
    if (digit < '0' || digit > '9') {
      return false;
    }
    sum += (digit - '0') * (index % 2 == 0 ? 1 : 3);
  }
  const char checkDigit = isbn13[12];
  return checkDigit >= '0' && checkDigit <= '9' && (10 - (sum % 10)) % 10 == checkDigit - '0';
}

bool isLowercaseSha256(const std::string& sha256) {
  if (sha256.size() != 64) {
    return false;
  }
  return std::all_of(sha256.begin(), sha256.end(), [](const char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
  });
}
}  // namespace

bool isReadingSyncBookIdBounded(const std::string& bookId) { return isBookIdSafe(bookId); }

bool isReadingSyncMetadataBounded(const ReadingSyncMetadata& metadata, const bool requireSequence) {
  if (metadata.schemaVersion != 1 || (requireSequence && metadata.sequence == 0) ||
      !isReadingSyncBookIdBounded(metadata.bookId) || metadata.title.empty() || !hasNonWhitespace(metadata.title) ||
      !utf16LengthWithin(metadata.title, 300) || !utf16LengthWithin(metadata.author, 200) ||
      metadata.progressPercent > 100) {
    return false;
  }
  if ((!metadata.lastReadAt.empty() && !utf16LengthWithin(metadata.lastReadAt, 64)) ||
      (!metadata.isbn13.empty() && !isValidIsbn13(metadata.isbn13))) {
    return false;
  }

  const bool hasCoverHash = !metadata.coverSha256.empty();
  const bool hasCoverMime = !metadata.coverMime.empty();
  if (hasCoverHash != hasCoverMime) {
    return false;
  }
  return !hasCoverHash || (isLowercaseSha256(metadata.coverSha256) &&
                           (metadata.coverMime == "image/jpeg" || metadata.coverMime == "image/png"));
}

bool validateReadingSyncResponse(const ReadingSyncWireResponse& wire, const uint32_t expectedSequence,
                                 ReadingSyncResponse& out) {
  if (!wire.hasStatus || !wire.hasSequence || !wire.hasLastAcceptedSequence || !wire.hasCoverRequired) {
    return false;
  }
  if (wire.sequence == 0 || wire.sequence > UINT32_MAX || wire.lastAcceptedSequence > UINT32_MAX ||
      wire.sequence != expectedSequence) {
    return false;
  }

  ReadingSyncServerStatus status = ReadingSyncServerStatus::Unknown;
  if (wire.status == "accepted") {
    status = ReadingSyncServerStatus::Accepted;
  } else if (wire.status == "duplicate") {
    status = ReadingSyncServerStatus::Duplicate;
  } else if (wire.status == "stale") {
    status = ReadingSyncServerStatus::Stale;
  } else {
    return false;
  }

  if ((status == ReadingSyncServerStatus::Accepted || status == ReadingSyncServerStatus::Duplicate) &&
      wire.lastAcceptedSequence < wire.sequence) {
    return false;
  }
  if (status == ReadingSyncServerStatus::Stale && (wire.lastAcceptedSequence <= wire.sequence || wire.coverRequired)) {
    return false;
  }

  ReadingSyncResponse validated;
  validated.status = status;
  validated.sequence = static_cast<uint32_t>(wire.sequence);
  validated.lastAcceptedSequence = static_cast<uint32_t>(wire.lastAcceptedSequence);
  validated.coverRequired = wire.coverRequired;
  out = validated;
  return true;
}
