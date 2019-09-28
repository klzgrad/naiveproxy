// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_MAP_UTIL_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_MAP_UTIL_H_

#include "net/quic/platform/impl/quic_map_util_impl.h"

namespace quic {

template <class Collection, class Key>
bool QuicContainsKey(const Collection& collection, const Key& key) {
  return QuicContainsKeyImpl(collection, key);
}

template <typename Collection, typename Value>
bool QuicContainsValue(const Collection& collection, const Value& value) {
  return QuicContainsValueImpl(collection, value);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_MAP_UTIL_H_
