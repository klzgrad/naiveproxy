// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_LRU_CACHE_H_
#define QUICHE_QUIC_CORE_QUIC_LRU_CACHE_H_

#include <memory>

#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

// A LRU cache that maps from type Key to Value* in QUIC.
// This cache CANNOT be shared by multiple threads (even with locks) because
// Value* returned by Lookup() can be invalid if the entry is evicted by other
// threads.
template <class K, class V>
class QUIC_NO_EXPORT QuicLRUCache {
 public:
  explicit QuicLRUCache(size_t capacity) : capacity_(capacity) {}
  QuicLRUCache(const QuicLRUCache&) = delete;
  QuicLRUCache& operator=(const QuicLRUCache&) = delete;

  // Inserts one unit of |key|, |value| pair to the cache. Cache takes ownership
  // of inserted |value|.
  void Insert(const K& key, std::unique_ptr<V> value) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      cache_.erase(it);
    }
    cache_.emplace(key, std::move(value));

    if (cache_.size() > capacity_) {
      cache_.pop_front();
    }
    DCHECK_LE(cache_.size(), capacity_);
  }

  // If cache contains an entry for |key|, return a pointer to it. This returned
  // value is guaranteed to be valid until Insert or Clear.
  // Else return nullptr.
  V* Lookup(const K& key) {
    auto it = cache_.find(key);
    if (it == cache_.end()) {
      return nullptr;
    }

    std::unique_ptr<V> value = std::move(it->second);
    cache_.erase(it);
    auto result = cache_.emplace(key, std::move(value));
    DCHECK(result.second);
    return result.first->second.get();
  }

  // Removes all entries from the cache.
  void Clear() { cache_.clear(); }

  // Returns maximum size of the cache.
  size_t MaxSize() const { return capacity_; }

  // Returns current size of the cache.
  size_t Size() const { return cache_.size(); }

 private:
  QuicLinkedHashMap<K, std::unique_ptr<V>> cache_;
  const size_t capacity_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_LRU_CACHE_H_
