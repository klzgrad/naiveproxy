// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_MAP_UTIL_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_MAP_UTIL_H_

#include "net/quiche/common/platform/impl/quiche_map_util_impl.h"

namespace quiche {

template <class Collection, class Key>
bool QuicheContainsKey(const Collection& collection, const Key& key) {
  return QuicheContainsKeyImpl(collection, key);
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_MAP_UTIL_H_
