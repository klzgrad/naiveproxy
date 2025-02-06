// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CLIENT_STATS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CLIENT_STATS_IMPL_H_

#include <string>

namespace quiche {

// Use namespace qualifier in case the macro is used outside the quiche
// namespace.

#define QUICHE_CLIENT_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring) \
  do {                                                                        \
    quiche::QuicheClientSparseHistogramImpl(name, static_cast<int>(sample));  \
  } while (0)

#define QUICHE_CLIENT_HISTOGRAM_BOOL_IMPL(name, sample, docstring) \
  do {                                                             \
    (void)sample; /* Workaround for -Wunused-variable. */          \
  } while (0)

#define QUICHE_CLIENT_HISTOGRAM_TIMES_IMPL(name, sample, min, max, \
                                           num_buckets, docstring) \
  do {                                                             \
    (void)sample; /* Workaround for -Wunused-variable. */          \
  } while (0)

#define QUICHE_CLIENT_HISTOGRAM_COUNTS_IMPL(name, sample, min, max, \
                                            num_buckets, docstring) \
  do {                                                              \
    quiche::QuicheClientSparseHistogramImpl(name, sample);          \
  } while (0)

inline void QuicheClientSparseHistogramImpl(const std::string& /*name*/,
                                            int /*sample*/) {
  // No-op.
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_CLIENT_STATS_IMPL_H_
