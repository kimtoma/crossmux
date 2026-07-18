#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

class ReadingSyncRawRequestState {
 public:
  static constexpr size_t kTokenBodyCapacity = 256;
  using TokenBodyBuffer = std::array<char, kTokenBodyCapacity + 1>;

  void start() {
    tokenBody_.fill('\0');
    size_ = 0;
    overflowed_ = false;
    complete_ = false;
  }

  void append(const uint8_t* bytes, const size_t byteCount) {
    if (complete_ || overflowed_ || (bytes == nullptr && byteCount != 0)) {
      markOverflow();
      return;
    }
    if (byteCount > kTokenBodyCapacity - size_) {
      markOverflow();
      return;
    }
    if (byteCount != 0) {
      std::memcpy(tokenBody_.data() + size_, bytes, byteCount);
      size_ += byteCount;
      tokenBody_[size_] = '\0';
    }
  }

  void finish() { complete_ = true; }

  void abort() {
    markOverflow();
    complete_ = false;
  }

  bool complete() const { return complete_; }
  bool overflowed() const { return overflowed_; }
  size_t size() const { return size_; }
  char* data() { return tokenBody_.data(); }
  const char* data() const { return tokenBody_.data(); }

 private:
  void markOverflow() {
    tokenBody_.fill('\0');
    size_ = 0;
    overflowed_ = true;
  }

  TokenBodyBuffer tokenBody_ = {};
  size_t size_ = 0;
  bool overflowed_ = false;
  bool complete_ = false;
};
