// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_ESTIMATE_MEMORY_USAGE_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_ESTIMATE_MEMORY_USAGE_H_

#include <cstddef>

#include "net/spdy/platform/impl/spdy_estimate_memory_usage_impl.h"

namespace spdy {

template <class T>
size_t SpdyEstimateMemoryUsage(const T& object) {
  return SpdyEstimateMemoryUsageImpl(object);
}

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_ESTIMATE_MEMORY_USAGE_H_
