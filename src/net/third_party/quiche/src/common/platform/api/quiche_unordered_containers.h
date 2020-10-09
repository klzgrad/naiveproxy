// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_UNORDERED_CONTAINERS_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_UNORDERED_CONTAINERS_H_

#include <functional>

#include "net/quiche/common/platform/impl/quiche_unordered_containers_impl.h"

namespace quiche {

// The default hasher used by hash tables.
template <typename Key>
using QuicheDefaultHasher = QuicheDefaultHasherImpl<Key>;

// A general-purpose unordered map.
template <typename Key,
          typename Value,
          typename Hash = QuicheDefaultHasher<Key>,
          typename Eq = std::equal_to<Key>>
using QuicheUnorderedMap = QuicheUnorderedMapImpl<Key, Value, Hash, Eq>;

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_UNORDERED_CONTAINERS_H_
