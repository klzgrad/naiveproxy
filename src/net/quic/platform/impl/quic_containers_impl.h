// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_CONTAINERS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_CONTAINERS_IMPL_H_

#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "base/containers/queue.h"
#include "base/containers/small_map.h"
#include "net/third_party/quiche/src/common/simple_linked_hash_map.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

// The default hasher used by hash tables.
template <typename Key>
using QuicDefaultHasherImpl = std::hash<Key>;

// TODO(mpw): s/std::unordered_map/gtl::node_hash_map/ once node_hash_map is
//   PG3-compatible.
template <typename Key,
          typename Value,
          typename Hash,
          typename Eq =
              typename std::unordered_map<Key, Value, Hash>::key_equal,
          typename Alloc =
              typename std::unordered_map<Key, Value, Hash>::allocator_type>
using QuicUnorderedMapImpl = std::unordered_map<Key, Value, Hash, Eq, Alloc>;

template <typename Key, typename Value, typename Hash>
using QuicHashMapImpl = std::unordered_map<Key, Value, Hash>;

// TODO(mpw): s/std::unordered_set/gtl::node_hash_set/ once node_hash_set is
//   PG3-compatible.
template <typename Key,
          typename Hash,
          typename Eq = typename std::unordered_set<Key, Hash>::key_equal,
          typename Alloc =
              typename std::unordered_set<Key, Hash>::allocator_type>
using QuicUnorderedSetImpl = std::unordered_set<Key, Hash, Eq, Alloc>;

template <typename Key, typename Hash>
using QuicHashSetImpl = std::unordered_set<Key, Hash>;

// A map which offers insertion-ordered iteration.
template <typename Key, typename Value, typename Hash>
using QuicLinkedHashMapImpl = quiche::SimpleLinkedHashMap<Key, Value, Hash>;

// A map which is faster than (for example) hash_map for a certain number of
// unique key-value-pair elements, and upgrades itself to unordered_map when
// runs out of space.
template <typename Key, typename Value, int Size>
using QuicSmallMapImpl = base::small_map<std::unordered_map<Key, Value>, Size>;

// Represents a simple queue which may be backed by a list or
// a flat circular buffer.
//
// DOES NOT GUARANTEE POINTER STABILITY!
template <typename T>
using QuicQueueImpl = base::queue<T>;

// TODO(wub): Switch to absl::InlinedVector once it is allowed.
template <typename T, size_t N, typename A = std::allocator<T>>
using QuicInlinedVectorImpl = std::vector<T, A>;

}  // namespace quic

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_CONTAINERS_IMPL_H_
