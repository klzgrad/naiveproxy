// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_MAP_UTIL_IMPL_H_
#define NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_MAP_UTIL_IMPL_H_

#include "base/stl_util.h"

namespace quiche {

template <class Collection, class Key>
bool QuicheContainsKeyImpl(const Collection& collection, const Key& key) {
  return base::Contains(collection, key);
}

}  // namespace quiche

#endif  // NET_QUICHE_COMMON_PLATFORM_IMPL_QUICHE_MAP_UTIL_IMPL_H_
