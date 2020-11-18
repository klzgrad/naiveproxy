// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_PTR_UTIL_IMPL_H_
#define NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_PTR_UTIL_IMPL_H_

#include <memory>
#include <utility>

#include "base/memory/ptr_util.h"

namespace quiche {

template <typename T>
std::unique_ptr<T> QuicheWrapUniqueImpl(T* ptr) {
  return base::WrapUnique<T>(ptr);
}

}  // namespace quiche

#endif  // NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_PTR_UTIL_IMPL_H_
