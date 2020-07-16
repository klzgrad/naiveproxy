// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP2_PLATFORM_IMPL_HTTP2_ESTIMATE_MEMORY_USAGE_IMPL_H_
#define NET_HTTP2_PLATFORM_IMPL_HTTP2_ESTIMATE_MEMORY_USAGE_IMPL_H_

#include <cstddef>

#include "base/trace_event/memory_usage_estimator.h"

namespace http2 {

template <class T>
size_t Http2EstimateMemoryUsageImpl(const T& object) {
  return base::trace_event::EstimateMemoryUsage(object);
}

}  // namespace http2

#endif  // NET_HTTP2_PLATFORM_IMPL_HTTP2_ESTIMATE_MEMORY_USAGE_IMPL_H_
