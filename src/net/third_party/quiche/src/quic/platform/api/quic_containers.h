// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_

#include "net/quic/platform/impl/quic_containers_impl.h"

namespace quic {

// The default hasher used by hash tables.
template <typename Key>
using QuicDefaultHasher = QuicDefaultHasherImpl<Key>;

// A general-purpose unordered map.
template <typename Key, typename Value, typename Hash = QuicDefaultHasher<Key>>
using QuicUnorderedMap = QuicUnorderedMapImpl<Key, Value, Hash>;

// A general-purpose unordered set.
template <typename Key, typename Hash = QuicDefaultHasher<Key>>
using QuicUnorderedSet = QuicUnorderedSetImpl<Key, Hash>;

// A map which offers insertion-ordered iteration.
template <typename Key, typename Value, typename Hash = QuicDefaultHasher<Key>>
using QuicLinkedHashMap = QuicLinkedHashMapImpl<Key, Value, Hash>;

// Used for maps that are typically small, then it is faster than (for example)
// hash_map which is optimized for large data sets. QuicSmallMap upgrades itself
// automatically to a QuicSmallMapImpl-specified map when it runs out of space.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename Key, typename Value, int Size>
using QuicSmallMap = QuicSmallMapImpl<Key, Value, Size>;

// Represents a simple queue which may be backed by a list or
// a flat circular buffer.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename T>
using QuicQueue = QuicQueueImpl<T>;

// A vector optimized for small sizes. Provides the same APIs as a std::vector.
template <typename T, size_t N, typename A = std::allocator<T>>
using QuicInlinedVector = QuicInlinedVectorImpl<T, N, A>;

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
