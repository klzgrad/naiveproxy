// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_METRICS_HISTOGRAM_MACROS_INTERNAL_H_
#define BASE_METRICS_HISTOGRAM_MACROS_INTERNAL_H_

#include <stdint.h>

#include <limits>
#include <type_traits>

#include "base/atomicops.h"
#include "base/logging.h"
#include "base/metrics/histogram.h"
#include "base/metrics/sparse_histogram.h"
#include "base/time/time.h"

// This is for macros internal to base/metrics. They should not be used outside
// of this directory. For writing to UMA histograms, see histogram_macros.h.

// TODO(rkaplow): Improve commenting of these methods.

//------------------------------------------------------------------------------
// Histograms are often put in areas where they are called many many times, and
// performance is critical.  As a result, they are designed to have a very low
// recurring cost of executing (adding additional samples). Toward that end,
// the macros declare a static pointer to the histogram in question, and only
// take a "slow path" to construct (or find) the histogram on the first run
// through the macro. We leak the histograms at shutdown time so that we don't
// have to validate using the pointers at any time during the running of the
// process.


// In some cases (integration into 3rd party code), it's useful to separate the
// definition of |atomic_histogram_pointer| from its use. To achieve this we
// define HISTOGRAM_POINTER_USE, which uses an |atomic_histogram_pointer|, and
// STATIC_HISTOGRAM_POINTER_BLOCK, which defines an |atomic_histogram_pointer|
// and forwards to HISTOGRAM_POINTER_USE.
#define HISTOGRAM_POINTER_USE(atomic_histogram_pointer,                        \
                              constant_histogram_name,                         \
                              histogram_add_method_invocation,                 \
                              histogram_factory_get_invocation)                \
  do {                                                                         \
    /*                                                                         \
     * Acquire_Load() ensures that we acquire visibility to the                \
     * pointed-to data in the histogram.                                       \
     */                                                                        \
    base::HistogramBase* histogram_pointer(                                    \
        reinterpret_cast<base::HistogramBase*>(                                \
            base::subtle::Acquire_Load(atomic_histogram_pointer)));            \
    if (!histogram_pointer) {                                                  \
      /*                                                                       \
       * This is the slow path, which will construct OR find the               \
       * matching histogram.  histogram_factory_get_invocation includes        \
       * locks on a global histogram name map and is completely thread         \
       * safe.                                                                 \
       */                                                                      \
      histogram_pointer = histogram_factory_get_invocation;                    \
                                                                               \
      /*                                                                       \
       * Use Release_Store to ensure that the histogram data is made           \
       * available globally before we make the pointer visible. Several        \
       * threads may perform this store, but the same value will be            \
       * stored in all cases (for a given named/spec'ed histogram).            \
       * We could do this without any barrier, since FactoryGet entered        \
       * and exited a lock after construction, but this barrier makes          \
       * things clear.                                                         \
       */                                                                      \
      base::subtle::Release_Store(                                             \
          atomic_histogram_pointer,                                            \
          reinterpret_cast<base::subtle::AtomicWord>(histogram_pointer));      \
    }                                                                          \
    if (DCHECK_IS_ON())                                                        \
      histogram_pointer->CheckName(constant_histogram_name);                   \
    histogram_pointer->histogram_add_method_invocation;                        \
  } while (0)

// This is a helper macro used by other macros and shouldn't be used directly.
// Defines the static |atomic_histogram_pointer| and forwards to
// HISTOGRAM_POINTER_USE.
#define STATIC_HISTOGRAM_POINTER_BLOCK(constant_histogram_name,                \
                                       histogram_add_method_invocation,        \
                                       histogram_factory_get_invocation)       \
  do {                                                                         \
    /*                                                                         \
     * The pointer's presence indicates that the initialization is complete.   \
     * Initialization is idempotent, so it can safely be atomically repeated.  \
     */                                                                        \
    static base::subtle::AtomicWord atomic_histogram_pointer = 0;              \
    HISTOGRAM_POINTER_USE(&atomic_histogram_pointer, constant_histogram_name,  \
                          histogram_add_method_invocation,                     \
                          histogram_factory_get_invocation);                   \
  } while (0)

// This is a helper macro used by other macros and shouldn't be used directly.
#define INTERNAL_HISTOGRAM_CUSTOM_COUNTS_WITH_FLAG(name, sample, min, max,     \
                                                   bucket_count, flag)         \
    STATIC_HISTOGRAM_POINTER_BLOCK(                                            \
        name, Add(sample),                                                     \
        base::Histogram::FactoryGet(name, min, max, bucket_count, flag))

// This is a helper macro used by other macros and shouldn't be used directly.
// The bucketing scheme is linear with a bucket size of 1. For N items,
// recording values in the range [0, N - 1] creates a linear histogram with N +
// 1 buckets:
//   [0, 1), [1, 2), ..., [N - 1, N)
// and an overflow bucket [N, infinity).
//
// Code should never emit to the overflow bucket; only to the other N buckets.
// This allows future versions of Chrome to safely increase the boundary size.
// Otherwise, the histogram would have [N - 1, infinity) as its overflow bucket,
// and so the maximal value (N - 1) would be emitted to this overflow bucket.
// But, if an additional value were later added, the bucket label for
// the value (N - 1) would change to [N - 1, N), which would result in different
// versions of Chrome using different bucket labels for identical data.
#define INTERNAL_HISTOGRAM_EXACT_LINEAR_WITH_FLAG(name, sample, boundary,  \
                                                  flag)                    \
  do {                                                                     \
    static_assert(!std::is_enum<decltype(sample)>::value,                  \
                  "|sample| should not be an enum type!");                 \
    static_assert(!std::is_enum<decltype(boundary)>::value,                \
                  "|boundary| should not be an enum type!");               \
    STATIC_HISTOGRAM_POINTER_BLOCK(                                        \
        name, Add(sample),                                                 \
        base::LinearHistogram::FactoryGet(name, 1, boundary, boundary + 1, \
                                          flag));                          \
  } while (0)

// Similar to the previous macro but intended for enumerations. This delegates
// the work to the previous macro, but supports scoped enumerations as well by
// forcing an explicit cast to the HistogramBase::Sample integral type.
//
// Note the range checks verify two separate issues:
// - that the declared enum size isn't out of range of HistogramBase::Sample
// - that the declared enum size is > 0
//
// TODO(dcheng): This should assert that the passed in types are actually enum
// types.
#define INTERNAL_HISTOGRAM_ENUMERATION_WITH_FLAG(name, sample, boundary, flag) \
  do {                                                                         \
    using decayed_sample = std::decay<decltype(sample)>::type;                 \
    using decayed_boundary = std::decay<decltype(boundary)>::type;             \
    static_assert(!std::is_enum<decayed_boundary>::value ||                    \
                      std::is_enum<decayed_sample>::value,                     \
                  "Unexpected: |boundary| is enum, but |sample| is not.");     \
    static_assert(!std::is_enum<decayed_sample>::value ||                      \
                      !std::is_enum<decayed_boundary>::value ||                \
                      std::is_same<decayed_sample, decayed_boundary>::value,   \
                  "|sample| and |boundary| shouldn't be of different enums");  \
    static_assert(                                                             \
        static_cast<uintmax_t>(boundary) <                                     \
            static_cast<uintmax_t>(                                            \
                std::numeric_limits<base::HistogramBase::Sample>::max()),      \
        "|boundary| is out of range of HistogramBase::Sample");                \
    INTERNAL_HISTOGRAM_EXACT_LINEAR_WITH_FLAG(                                 \
        name, static_cast<base::HistogramBase::Sample>(sample),                \
        static_cast<base::HistogramBase::Sample>(boundary), flag);             \
  } while (0)

// This is a helper macro used by other macros and shouldn't be used directly.
// This is necessary to expand __COUNTER__ to an actual value.
#define INTERNAL_SCOPED_UMA_HISTOGRAM_TIMER_EXPANDER(name, is_long, key)       \
  INTERNAL_SCOPED_UMA_HISTOGRAM_TIMER_UNIQUE(name, is_long, key)

// This is a helper macro used by other macros and shouldn't be used directly.
#define INTERNAL_SCOPED_UMA_HISTOGRAM_TIMER_UNIQUE(name, is_long, key)         \
  class ScopedHistogramTimer##key {                                            \
   public:                                                                     \
    ScopedHistogramTimer##key() : constructed_(base::TimeTicks::Now()) {}      \
    ~ScopedHistogramTimer##key() {                                             \
      base::TimeDelta elapsed = base::TimeTicks::Now() - constructed_;         \
      if (is_long) {                                                           \
        UMA_HISTOGRAM_LONG_TIMES_100(name, elapsed);                           \
      } else {                                                                 \
        UMA_HISTOGRAM_TIMES(name, elapsed);                                    \
      }                                                                        \
    }                                                                          \
   private:                                                                    \
    base::TimeTicks constructed_;                                              \
  } scoped_histogram_timer_##key

// Macro for sparse histogram.
// The implementation is more costly to add values to, and each value
// stored has more overhead, compared to the other histogram types. However it
// may be more efficient in memory if the total number of sample values is small
// compared to the range of their values.
#define INTERNAL_HISTOGRAM_SPARSE_SLOWLY(name, sample)                         \
    do {                                                                       \
      base::HistogramBase* histogram = base::SparseHistogram::FactoryGet(      \
          name, base::HistogramBase::kUmaTargetedHistogramFlag);               \
      histogram->Add(sample);                                                  \
    } while (0)

#endif  // BASE_METRICS_HISTOGRAM_MACROS_INTERNAL_H_
