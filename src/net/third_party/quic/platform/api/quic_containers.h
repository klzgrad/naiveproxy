// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_

#include "net/third_party/quic/platform/impl/quic_containers_impl.h"

namespace quic {

// A general-purpose unordered map.
template <typename Key,
          typename Value,
          typename Hash = typename QuicUnorderedMapImpl<Key, Value>::hasher,
          typename Eq = typename QuicUnorderedMapImpl<Key, Value>::key_equal,
          typename Alloc =
              typename QuicUnorderedMapImpl<Key, Value>::allocator_type>
using QuicUnorderedMap = QuicUnorderedMapImpl<Key, Value, Hash, Eq, Alloc>;

// A general-purpose unordered set.
template <typename Key,
          typename Hash = typename QuicUnorderedSetImpl<Key>::hasher,
          typename Eq = typename QuicUnorderedSetImpl<Key>::key_equal,
          typename Alloc = typename QuicUnorderedSetImpl<Key>::allocator_type>
using QuicUnorderedSet = QuicUnorderedSetImpl<Key, Hash, Eq, Alloc>;

// A map which offers insertion-ordered iteration.
template <typename Key, typename Value>
using QuicLinkedHashMap = QuicLinkedHashMapImpl<Key, Value>;

// Used for maps that are typically small, then it is faster than (for example)
// hash_map which is optimized for large data sets. QuicSmallMap upgrades itself
// automatically to a QuicSmallMapImpl-specified map when it runs out of space.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename Key, typename Value, int Size>
using QuicSmallMap = QuicSmallMapImpl<Key, Value, Size>;

// A data structure used to represent a sorted set of non-empty, non-adjacent,
// and mutually disjoint intervals.
template <typename T>
using QuicIntervalSet = QuicIntervalSetImpl<T>;

// Represents a simple queue which may be backed by a list or
// a flat circular buffer.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename T>
using QuicQueue = QuicQueueImpl<T>;

// Represents a double-ended queue which may be backed by a list or
// a flat circular buffer.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename T>
using QuicDeque = QuicDequeImpl<T>;

// A vector optimized for small sizes. Provides the same APIs as a std::vector.
template <typename T, size_t N, typename A = std::allocator<T>>
using QuicInlinedVector = QuicInlinedVectorImpl<T, N, A>;

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
