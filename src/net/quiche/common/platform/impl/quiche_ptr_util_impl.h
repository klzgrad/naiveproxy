// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_PTR_UTIL_IMPL_H_
#define NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_PTR_UTIL_IMPL_H_

#include <memory>

namespace quiche {

template <typename T, typename... Args>
std::unique_ptr<T> QuicheMakeUniqueImpl(Args&&... args) {
  return std::make_unique<T>(std::forward<Args>(args)...);
}

}  // namespace quiche

#endif  // NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_PTR_UTIL_IMPL_H_
