// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_

#include "net/quic/platform/impl/quic_containers_impl.h"

#include "absl/hash/hash.h"

namespace quic {

// A map which offers insertion-ordered iteration.
template <typename Key, typename Value, typename Hash = absl::Hash<Key>>
using QuicLinkedHashMap = QuicLinkedHashMapImpl<Key, Value, Hash>;

// A vector optimized for small sizes. Provides the same APIs as a std::vector.
template <typename T, size_t N, typename A = std::allocator<T>>
using QuicInlinedVector = QuicInlinedVectorImpl<T, N, A>;

// An ordered set of values.
//
// DOES NOT GUARANTEE POINTER OR ITERATOR STABILITY!
template <typename Key,
          typename Compare = std::less<Key>,
          typename Rep = std::vector<Key>>
using QuicOrderedSet = QuicOrderedSetImpl<Key, Compare, Rep>;

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_CONTAINERS_H_
