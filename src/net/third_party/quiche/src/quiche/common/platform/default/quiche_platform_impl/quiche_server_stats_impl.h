// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_SERVER_STATS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_SERVER_STATS_IMPL_H_

#define QUICHE_SERVER_HISTOGRAM_ENUM_IMPL(name, sample, enum_size, docstring) \
  do {                                                                        \
  } while (0)

#define QUICHE_SERVER_HISTOGRAM_BOOL_IMPL(name, sample, docstring) \
  do {                                                             \
  } while (0)

#define QUICHE_SERVER_HISTOGRAM_TIMES_IMPL(name, sample, min, max,  \
                                           bucket_count, docstring) \
  do {                                                              \
  } while (0)

#define QUICHE_SERVER_HISTOGRAM_COUNTS_IMPL(name, sample, min, max,  \
                                            bucket_count, docstring) \
  do {                                                               \
  } while (0)

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_SERVER_STATS_IMPL_H_
