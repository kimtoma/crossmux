#pragma once

#include <Utf8.h>

#include <cstdint>

inline constexpr bool shouldInsertCjkSourceSpace(const bool explicitSpace, const bool segmentBreak, const uint32_t left,
                                                 const uint32_t right) {
  if (explicitSpace) return true;
  if (!segmentBreak || left == 0 || right == 0) return false;

  const bool leftNoSpaceCjk = utf8IsCjkBreakable(left) && !utf8IsHangul(left);
  const bool rightNoSpaceCjk = utf8IsCjkBreakable(right) && !utf8IsHangul(right);
  return !(leftNoSpaceCjk && rightNoSpaceCjk);
}
