// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_LRU_CACHE_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_LRU_CACHE_IMPL_H_

#include "base/containers/mru_cache.h"

namespace net {

template <class K, class V>
class QuicLRUCacheImpl {
 public:
  explicit QuicLRUCacheImpl(int64_t total_units) : mru_cache_(total_units) {}

  // Inserts one unit of |key|, |value| pair to the cache.
  void Insert(const K& key, std::unique_ptr<V> value) {
    mru_cache_.Put(key, std::move(value));
  }

  // If cache contains an entry for |key|, return a pointer to it. This returned
  // value is guaranteed to be valid until Insert or Clear.
  // Else return nullptr.
  V* Lookup(const K& key) {
    auto cached_it = mru_cache_.Get(key);
    if (cached_it != mru_cache_.end()) {
      return cached_it->second.get();
    }
    return nullptr;
  }

  // Removes all entries from the cache.
  void Clear() { mru_cache_.Clear(); }

  // Returns maximum size of the cache.
  int64_t MaxSize() const { return mru_cache_.max_size(); }

  // Returns current size of the cache.
  int64_t Size() const { return mru_cache_.size(); }

 private:
  base::MRUCache<K, std::unique_ptr<V>> mru_cache_;

  DISALLOW_COPY_AND_ASSIGN(QuicLRUCacheImpl);
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_LRU_CACHE_IMPL_H_
