// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_CHECKED_MATH_H_
#define QUICHE_COMMON_QUICHE_CHECKED_MATH_H_

#include <concepts>
#include <optional>

#include "absl/base/config.h"
#include "absl/types/span.h"

namespace quiche {

// Adds two unsigned integers, `a` and `b`.  Returns the sum if the resulting
// sum fits into the specified integer type, or nullopt if it does not.
template <typename T>
  requires(std::unsigned_integral<T>)
std::optional<T> SafeAdd(T a, T b) {
  T out;
#if ABSL_HAVE_BUILTIN(__builtin_add_overflow)
  if (__builtin_add_overflow(a, b, &out)) {
    return std::nullopt;
  }
#else
  out = a + b;
  if (out < a) {
    return std::nullopt;
  }
#endif
  return out;
}

// Adds two unsigned integers, `a` and `b`.  If the resulting sum fits into the
// specified integer type, sets `a` to that sum value, and returns true;
// otherwise, returns false.
template <typename T>
  requires(std::unsigned_integral<T>)
[[nodiscard]] bool SafeIncrementBy(T& a, T b) {
  std::optional<T> out = SafeAdd(a, b);
  if (!out.has_value()) {
    return false;
  }
  a = *out;
  return true;
}

// Sums all of the provided arguments, and returns the sum if it fits into the
// resulting integer, or nullopt if it overflows.
template <typename T>
  requires(std::unsigned_integral<T>)
std::optional<T> SafeSum(absl::Span<const T> summands) {
  T out = 0;
  for (const T num : summands) {
    if (!SafeIncrementBy(out, num)) {
      return std::nullopt;
    }
  }
  return out;
}

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_CHECKED_MATH_H_
