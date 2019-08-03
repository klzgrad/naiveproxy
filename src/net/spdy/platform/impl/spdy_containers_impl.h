// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_SPDY_PLATFORM_IMPL_SPDY_CONTAINERS_IMPL_H_
#define NET_SPDY_PLATFORM_IMPL_SPDY_CONTAINERS_IMPL_H_

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "base/strings/string_piece.h"
#include "net/third_party/quiche/src/common/simple_linked_hash_map.h"

namespace spdy {

template <typename KeyType>
using SpdyHashImpl = std::hash<KeyType>;

template <typename KeyType, typename ValueType, typename Hash>
using SpdyHashMapImpl = std::unordered_map<KeyType, ValueType, Hash>;

template <typename ElementType, typename Hasher, typename Eq>
using SpdyHashSetImpl = std::unordered_set<ElementType, Hasher, Eq>;

template <typename Key, typename Value, typename Hash>
using SpdyLinkedHashMapImpl = quiche::SimpleLinkedHashMap<Key, Value, Hash>;

template <typename T, size_t N, typename A = std::allocator<T>>
using SpdyInlinedVectorImpl = std::vector<T, A>;

using SpdyStringPieceHashImpl = base::StringPieceHash;

inline size_t SpdyHashStringPairImpl(SpdyStringPiece a, SpdyStringPiece b) {
  return base::StringPieceHash()(a) ^ base::StringPieceHash()(b);
}

}  // namespace spdy

#endif  // NET_SPDY_PLATFORM_IMPL_SPDY_CONTAINERS_IMPL_H_
