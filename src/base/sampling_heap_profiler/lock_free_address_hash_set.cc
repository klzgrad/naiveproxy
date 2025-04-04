// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sampling_heap_profiler/lock_free_address_hash_set.h"

#include <bit>
#include <limits>

#include "base/containers/contains.h"
#include "base/synchronization/lock.h"

namespace base {

LockFreeAddressHashSet::LockFreeAddressHashSet(size_t buckets_count, Lock& lock)
    : lock_(lock), buckets_(buckets_count), bucket_mask_(buckets_count - 1) {
  DCHECK(std::has_single_bit(buckets_count));
  DCHECK_LE(bucket_mask_, std::numeric_limits<uint32_t>::max());
}

LockFreeAddressHashSet::~LockFreeAddressHashSet() {
  for (std::atomic<Node*>& bucket : buckets_) {
    Node* node = bucket.load(std::memory_order_relaxed);
    while (node) {
      Node* next = node->next;
      delete node;
      node = next;
    }
  }
}

void LockFreeAddressHashSet::Insert(void* key) {
  lock_->AssertAcquired();
  DCHECK_NE(key, nullptr);
  CHECK(!Contains(key));
  ++size_;
  // Note: There's no need to use std::atomic_compare_exchange here,
  // as we do not support concurrent inserts, so values cannot change midair.
  std::atomic<Node*>& bucket = buckets_[Hash(key) & bucket_mask_];
  Node* node = bucket.load(std::memory_order_relaxed);
  // First iterate over the bucket nodes and try to reuse an empty one if found.
  for (; node != nullptr; node = node->next) {
    if (node->key.load(std::memory_order_relaxed) == nullptr) {
      node->key.store(key, std::memory_order_relaxed);
      return;
    }
  }
  // There are no empty nodes to reuse left in the bucket.
  // Create a new node first...
  Node* new_node = new Node(key, bucket.load(std::memory_order_relaxed));
  // ... and then publish the new chain.
  bucket.store(new_node, std::memory_order_release);
}

void LockFreeAddressHashSet::Copy(const LockFreeAddressHashSet& other) {
  lock_->AssertAcquired();
  DCHECK_EQ(0u, size());
  for (const std::atomic<Node*>& bucket : other.buckets_) {
    for (Node* node = bucket.load(std::memory_order_relaxed); node;
         node = node->next) {
      void* key = node->key.load(std::memory_order_relaxed);
      if (key) {
        Insert(key);
      }
    }
  }
}

std::vector<size_t> LockFreeAddressHashSet::GetBucketLengths() const {
  lock_->AssertAcquired();
  std::vector<size_t> lengths;
  lengths.reserve(buckets_.size());
  for (const std::atomic<Node*>& bucket : buckets_) {
    size_t length = 0;
    for (Node* node = bucket.load(std::memory_order_relaxed); node != nullptr;
         node = node->next) {
      ++length;
    }
    lengths.push_back(length);
  }
  return lengths;
}

}  // namespace base
