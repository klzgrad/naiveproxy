// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_SERVER_STATS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_SERVER_STATS_H_

#include "quiche/common/platform/api/quiche_server_stats.h"

#define QUIC_SERVER_HISTOGRAM_ENUM(name, sample, enum_size, docstring) \
  QUICHE_SERVER_HISTOGRAM_ENUM(name, sample, enum_size, docstring)

#define QUIC_SERVER_HISTOGRAM_BOOL(name, sample, docstring) \
  QUICHE_SERVER_HISTOGRAM_BOOL(name, sample, docstring)

#define QUIC_SERVER_HISTOGRAM_TIMES(name, sample, min, max, bucket_count, \
                                    docstring)                            \
  QUICHE_SERVER_HISTOGRAM_TIMES(name, sample, min, max, bucket_count, docstring)

#define QUIC_SERVER_HISTOGRAM_COUNTS(name, sample, min, max, bucket_count, \
                                     docstring)                            \
  QUICHE_SERVER_HISTOGRAM_COUNTS(name, sample, min, max, bucket_count,     \
                                 docstring)

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_SERVER_STATS_H_
