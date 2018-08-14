// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_EXPORTED_STATS_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_EXPORTED_STATS_IMPL_H_

// By convention, all QUIC histograms are prefixed by "Net.".
#define QUIC_HISTOGRAM_NAME(raw_name) "Net." raw_name

#define QUIC_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring) \
  UMA_HISTOGRAM_ENUMERATION(QUIC_HISTOGRAM_NAME(name), sample, enum_size)

#define QUIC_HISTOGRAM_BOOL_IMPL(name, sample, docstring) \
  UMA_HISTOGRAM_BOOLEAN(QUIC_HISTOGRAM_NAME(name), sample)

#define QUIC_HISTOGRAM_TIMES_IMPL(name, sample, max, num_buckets, docstring)   \
  UMA_HISTOGRAM_CUSTOM_TIMES(                                                  \
      QUIC_HISTOGRAM_NAME(name), sample, base::TimeDelta::FromMilliseconds(1), \
      base::TimeDelta::FromMilliseconds(max), num_buckets)

#define QUIC_HISTOGRAM_COUNTS_IMPL(name, sample, max, num_buckets, docstring) \
  UMA_HISTOGRAM_CUSTOM_COUNTS(QUIC_HISTOGRAM_NAME(name), sample, 1, max,      \
                              num_buckets)

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_EXPORTED_STATS_IMPL_H_
