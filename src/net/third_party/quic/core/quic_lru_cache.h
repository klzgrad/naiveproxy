// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_LRU_CACHE_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_LRU_CACHE_H_

#include <memory>

#include "net/third_party/quic/platform/api/quic_containers.h"
#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_lru_cache.h"

namespace quic {

// A LRU cache that maps from type Key to Value* in QUIC.
// This cache CANNOT be shared by multiple threads (even with locks) because
// Value* returned by Lookup() can be invalid if the entry is evicted by other
// threads.
// TODO(vasilvv): rename this class when quic_new_lru_cache flag is deprecated.
template <class K, class V>
class QuicLRUCacheNew {
 public:
  explicit QuicLRUCacheNew(size_t capacity) : capacity_(capacity) {}
  QuicLRUCacheNew(const QuicLRUCacheNew&) = delete;
  QuicLRUCacheNew& operator=(const QuicLRUCacheNew&) = delete;

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
    QUIC_FLAG_COUNT(quic_reloadable_flag_quic_new_lru_cache);
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

// TODO(vasilvv): remove this class when quic_new_lru_cache flag is deprecated.
template <class K, class V>
class QuicLRUCache {
 public:
  explicit QuicLRUCache(size_t capacity)
      : QuicLRUCache(capacity, GetQuicReloadableFlag(quic_new_lru_cache)) {}
  QuicLRUCache(size_t capacity, bool use_new)
      : new_(capacity), old_(capacity), use_new_(use_new) {}
  QuicLRUCache(const QuicLRUCache&) = delete;
  QuicLRUCache& operator=(const QuicLRUCache&) = delete;

  void Insert(const K& key, std::unique_ptr<V> value) {
    if (use_new_) {
      new_.Insert(key, std::move(value));
    } else {
      old_.Insert(key, std::move(value));
    }
  }

  V* Lookup(const K& key) {
    if (use_new_) {
      return new_.Lookup(key);
    } else {
      return old_.Lookup(key);
    }
  }

  void Clear() {
    if (use_new_) {
      new_.Clear();
    } else {
      old_.Clear();
    }
  }

  size_t MaxSize() const {
    if (use_new_) {
      return new_.MaxSize();
    } else {
      return old_.MaxSize();
    }
  }

  size_t Size() const {
    if (use_new_) {
      return new_.Size();
    } else {
      return old_.Size();
    }
  }

 private:
  QuicLRUCacheNew<K, V> new_;
  QuicLRUCacheOld<K, V> old_;
  bool use_new_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_LRU_CACHE_H_
