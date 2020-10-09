// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_PTR_UTIL_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_PTR_UTIL_H_

#include <memory>

#include "net/quiche/common/platform/impl/quiche_ptr_util_impl.h"

namespace quiche {

template <typename T>
std::unique_ptr<T> QuicheWrapUnique(T* ptr) {
  return QuicheWrapUniqueImpl<T>(ptr);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_PTR_UTIL_H_
