// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_ESTIMATE_MEMORY_USAGE_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_ESTIMATE_MEMORY_USAGE_IMPL_H_

#include <cstddef>

namespace quiche {

// No-op implementation.
template <class T>
size_t QuicheEstimateMemoryUsageImpl(const T& /*object*/) {
  return 0;
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_ESTIMATE_MEMORY_USAGE_IMPL_H_
