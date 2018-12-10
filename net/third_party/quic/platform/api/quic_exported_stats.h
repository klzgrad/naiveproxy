// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_EXPORTED_STATS_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_EXPORTED_STATS_H_

#include "net/third_party/quic/platform/impl/quic_exported_stats_impl.h"

namespace quic {

// TODO(wub): Add support for counters. Only histograms are supported for now.

//------------------------------------------------------------------------------
// Enumeration histograms.
//
// Sample usage:
//   // In Chrome, these values are persisted to logs. Entries should not be
//   // renumbered and numeric values should never be reused.
//   enum class MyEnum {
//     FIRST_VALUE = 0,
//     SECOND_VALUE = 1,
//     ...
//     FINAL_VALUE = N,
//     COUNT
//   };
//   QUIC_HISTOGRAM_ENUM("My.Enumeration", MyEnum::SOME_VALUE, MyEnum::COUNT,
//                       "Number of time $foo equals to some enum value");
//
// Note: The value in |sample| must be strictly less than |enum_size|.

#define QUIC_HISTOGRAM_ENUM(name, sample, enum_size, docstring) \
  QUIC_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring)

//------------------------------------------------------------------------------
// Histogram for boolean values.

// Sample usage:
//   QUIC_HISTOGRAM_BOOL("My.Boolean", bool,
//                       "Number of times $foo is true or false");
#define QUIC_HISTOGRAM_BOOL(name, sample, docstring) \
  QUIC_HISTOGRAM_BOOL_IMPL(name, sample, docstring)

//------------------------------------------------------------------------------
// Timing histograms. These are used for collecting timing data (generally
// latencies).

// These macros create exponentially sized histograms (lengths of the bucket
// ranges exponentially increase as the sample range increases). The units for
// sample and max are unspecified, but they must be the same for one histogram.

// Sample usage:
//   QUIC_HISTOGRAM_TIMES("My.Timing.Histogram.InMs",
//                        sample,     // Time spent in milliseconds.
//                        10 * 1000,  // Record up to 10K milliseconds.
//                        "Time spent in doing something");

#define QUIC_HISTOGRAM_TIMES(name, sample, max, docstring) \
  QUIC_HISTOGRAM_TIMES_IMPL(name, sample, max, 50, docstring)

//------------------------------------------------------------------------------
// Count histograms. These are used for collecting numeric data.

// These macros default to exponential histograms - i.e. the lengths of the
// bucket ranges exponentially increase as the sample range increases.

// All of these macros must be called with |name| as a runtime constant.

// Sample usage:
//   QUIC_HISTOGRAM_COUNTS("My.Histogram",
//                         sample,    // Number of something in this event.
//                         1000,      // Record up to 1K of something.
//                         "Number of something.");

#define QUIC_HISTOGRAM_COUNTS(name, sample, max, docstring) \
  QUIC_HISTOGRAM_COUNTS_IMPL(name, sample, max, 50, docstring)

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_EXPORTED_STATS_H_
