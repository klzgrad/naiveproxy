// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_EXPORTED_STATS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_EXPORTED_STATS_H_

#include "net/quic/platform/impl/quic_client_stats_impl.h"
#include "net/quic/platform/impl/quic_server_stats_impl.h"

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

#define QUIC_HISTOGRAM_ENUM(name, sample, enum_size, docstring)          \
  do {                                                                   \
    QUIC_CLIENT_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring); \
    QUIC_SERVER_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring); \
  } while (0)

//------------------------------------------------------------------------------
// Histogram for boolean values.

// Sample usage:
//   QUIC_HISTOGRAM_BOOL("My.Boolean", bool,
//                       "Number of times $foo is true or false");
#define QUIC_HISTOGRAM_BOOL(name, sample, docstring)          \
  do {                                                        \
    QUIC_CLIENT_HISTOGRAM_BOOL_IMPL(name, sample, docstring); \
    QUIC_SERVER_HISTOGRAM_BOOL_IMPL(name, sample, docstring); \
  } while (0)

//------------------------------------------------------------------------------
// Timing histograms. These are used for collecting timing data (generally
// latencies).

// These macros create exponentially sized histograms (lengths of the bucket
// ranges exponentially increase as the sample range increases). The units for
// sample and max are unspecified, but they must be the same for one histogram.

// Sample usage:
//   QUIC_HISTOGRAM_TIMES("My.Timing.Histogram.InMs", time_delta,
//       QuicTime::Delta::FromSeconds(1), QuicTime::Delta::FromSecond(3600 *
//       24), 100, "Time spent in doing operation.");

#define QUIC_HISTOGRAM_TIMES(name, sample, min, max, bucket_count, docstring) \
  do {                                                                        \
    QUIC_CLIENT_HISTOGRAM_TIMES_IMPL(name, sample, min, max, bucket_count,    \
                                     docstring);                              \
    QUIC_SERVER_HISTOGRAM_TIMES_IMPL(name, sample, min, max, bucket_count,    \
                                     docstring);                              \
  } while (0)

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

#define QUIC_HISTOGRAM_COUNTS(name, sample, min, max, bucket_count, docstring) \
  do {                                                                         \
    QUIC_CLIENT_HISTOGRAM_COUNTS_IMPL(name, sample, min, max, bucket_count,    \
                                      docstring);                              \
    QUIC_SERVER_HISTOGRAM_COUNTS_IMPL(name, sample, min, max, bucket_count,    \
                                      docstring);                              \
  } while (0)

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_EXPORTED_STATS_H_
