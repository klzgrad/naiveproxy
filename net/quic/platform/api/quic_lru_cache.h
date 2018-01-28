// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_LRU_CACHE_H_
#define NET_QUIC_PLATFORM_API_QUIC_LRU_CACHE_H_

#include <memory>

#include "net/quic/platform/impl/quic_lru_cache_impl.h"

namespace net {

// A LRU cache that maps from type Key to Value* in QUIC.
// This cache CANNOT be shared by multiple threads (even with locks) because
// Value* returned by Lookup() can be invalid if the entry is evicted by other
// threads.
template <class K, class V>
class QuicLRUCache {
 public:
  explicit QuicLRUCache(int64_t total_units) : impl_(total_units) {}

  // Inserts one unit of |key|, |value| pair to the cache. Cache takes ownership
  // of inserted |value|.
  void Insert(const K& key, std::unique_ptr<V> value) {
    impl_.Insert(key, std::move(value));
  }

  // If cache contains an entry for |key|, return a pointer to it. This returned
  // value is guaranteed to be valid until Insert or Clear.
  // Else return nullptr.
  V* Lookup(const K& key) { return impl_.Lookup(key); }

  // Removes all entries from the cache. This method MUST be called before
  // destruction.
  void Clear() { impl_.Clear(); }

  // Returns maximum size of the cache.
  int64_t MaxSize() const { return impl_.MaxSize(); }

  // Returns current size of the cache.
  int64_t Size() const { return impl_.Size(); }

 private:
  QuicLRUCacheImpl<K, V> impl_;

  DISALLOW_COPY_AND_ASSIGN(QuicLRUCache);
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_LRU_CACHE_H_
