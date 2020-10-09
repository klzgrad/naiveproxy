// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_HTTP2_PLATFORM_API_HTTP2_ESTIMATE_MEMORY_USAGE_H_
#define QUICHE_HTTP2_PLATFORM_API_HTTP2_ESTIMATE_MEMORY_USAGE_H_

#include <cstddef>

#include "net/http2/platform/impl/http2_estimate_memory_usage_impl.h"

namespace http2 {

template <class T>
size_t Http2EstimateMemoryUsage(const T& object) {
  return Http2EstimateMemoryUsageImpl(object);
}

}  // namespace http2

#endif  // QUICHE_HTTP2_PLATFORM_API_HTTP2_ESTIMATE_MEMORY_USAGE_H_
