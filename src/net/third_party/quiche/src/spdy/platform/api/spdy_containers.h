// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_CONTAINERS_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_CONTAINERS_H_

#include "net/spdy/platform/impl/spdy_containers_impl.h"

namespace spdy {

template <typename KeyType>
using SpdyHash = SpdyHashImpl<KeyType>;

// SpdyHashMap does not guarantee pointer stability.
template <typename KeyType,
          typename ValueType,
          typename Hash = SpdyHash<KeyType>>
using SpdyHashMap = SpdyHashMapImpl<KeyType, ValueType, Hash>;

// SpdyHashSet does not guarantee pointer stability.
template <typename ElementType, typename Hasher, typename Eq>
using SpdyHashSet = SpdyHashSetImpl<ElementType, Hasher, Eq>;

// A map which offers insertion-ordered iteration.
template <typename Key, typename Value, typename Hash, typename Eq>
using SpdyLinkedHashMap = SpdyLinkedHashMapImpl<Key, Value, Hash, Eq>;

// A vector optimized for small sizes. Provides the same APIs as a std::vector.
template <typename T, size_t N, typename A = std::allocator<T>>
using SpdyInlinedVector = SpdyInlinedVectorImpl<T, N, A>;

// Used for maps that are typically small, then it is faster than (for example)
// hash_map which is optimized for large data sets. SpdySmallMap upgrades itself
// automatically to a SpdySmallMapImpl-specified map when it runs out of space.
template <typename Key, typename Value, int Size>
using SpdySmallMap = SpdySmallMapImpl<Key, Value, Size>;

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_CONTAINERS_H_
