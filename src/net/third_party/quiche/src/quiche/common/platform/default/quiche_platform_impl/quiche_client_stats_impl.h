// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CLIENT_STATS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CLIENT_STATS_IMPL_H_

#include <stddef.h>
#include <stdint.h>

#include <type_traits>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

// Force `name` to be a string constant by applying C-preprocessor
// concatenation.
#define QUICHE_REQUIRE_STRING_CONSTANT(name) "" name

// The implementations in this file are no-ops at runtime, but force type
// checking of the arguments at compile-time so that the code will still compile
// when merged to Chromium.

// Use namespace qualifier in case the macro is used outside the quiche
// namespace.

#define QUICHE_CLIENT_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring) \
  quiche::QuicheClientHistogramEnumerationTypeChecker(                        \
      QUICHE_REQUIRE_STRING_CONSTANT(name), sample, enum_size)

#define QUICHE_CLIENT_HISTOGRAM_BOOL_IMPL(name, sample, docstring) \
  quiche::QuicheClientHistogramBoolTypeChecker(                    \
      QUICHE_REQUIRE_STRING_CONSTANT(name), sample)

#define QUICHE_CLIENT_HISTOGRAM_TIMES_IMPL(name, sample, min, max, \
                                           num_buckets, docstring) \
  quiche::QuicheClientHistogramTimesTypeChecker(                   \
      QUICHE_REQUIRE_STRING_CONSTANT(name), sample, min, max, num_buckets)

#define QUICHE_CLIENT_HISTOGRAM_COUNTS_IMPL(name, sample, min, max, \
                                            num_buckets, docstring) \
  quiche::QuicheClientHistogramCountsTypeChecker(                   \
      QUICHE_REQUIRE_STRING_CONSTANT(name), sample, min, max, num_buckets)

// This object's constructor enforces the restriction that the argument must be
// a string constant.
struct QUICHE_NO_EXPORT QuicheRequireStringConstant {
  template <size_t N>
  consteval QuicheRequireStringConstant(const char (& /*name*/)[N]) {}
};

inline void QuicheClientSparseHistogramImpl(absl::string_view /*name*/,
                                            int /*sample*/) {
  // No-op.
}

// Enforce type checks on enums so that errors can be caught before rolling to
// Chromium.
template <typename SampleType, typename EnumSizeType>
inline void QuicheClientHistogramEnumerationTypeChecker(
    QuicheRequireStringConstant /*name*/, SampleType /*sample*/,
    EnumSizeType /*enum_size*/) {
  // These are equivalent to the type checks done by Chromium's
  // UMA_HISTOGRAM_ENUMERATION macro.
  static_assert(!std::is_enum_v<SampleType> || std::is_enum_v<EnumSizeType>,
                "Unexpected: |enum_size| is enum, but |sample| is not.");
  static_assert(
      !std::is_enum_v<SampleType> || std::is_same_v<SampleType, EnumSizeType>,
      "|sample| and |boundary| shouldn't be of different enums");
}

inline void QuicheClientHistogramBoolTypeChecker(
    QuicheRequireStringConstant /*name*/, bool /*sample*/) {}

template <typename TimeDelta>
inline void QuicheClientHistogramTimesTypeChecker(
    QuicheRequireStringConstant /*name*/, TimeDelta sample, TimeDelta /*min*/,
    TimeDelta /*max*/, int /*num_buckets*/) {
  static_assert(
      std::is_convertible_v<decltype(sample.ToMicroseconds()), int64_t>,
      "The value passed to QUICHE_CLIENT_HISTOGRAM_TIMES must have a "
      "ToMicroseconds() method that returns a value convertible to int64_t.");
}

inline void QuicheClientHistogramCountsTypeChecker(
    QuicheRequireStringConstant /*name*/, int32_t /*sample*/, int32_t /*min*/,
    int32_t /*max*/, size_t /*num_buckets*/) {}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CLIENT_STATS_IMPL_H_
