// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_MAP_UTIL_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_MAP_UTIL_H_

#include "net/spdy/platform/impl/spdy_map_util_impl.h"

namespace spdy {

template <class Collection, class Key>
bool SpdyContainsKey(const Collection& collection, const Key& key) {
  return SpdyContainsKeyImpl(collection, key);
}

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_MAP_UTIL_H_
