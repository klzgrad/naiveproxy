// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/sampling_heap_profiler/lock_free_address_hash_set.h"

#include <limits>

#include "base/bits.h"

namespace base {

LockFreeAddressHashSet::LockFreeAddressHashSet(size_t buckets_count)
    : buckets_(buckets_count), bucket_mask_(buckets_count - 1) {
  DCHECK(bits::IsPowerOfTwo(buckets_count));
  DCHECK(bucket_mask_ <= std::numeric_limits<uint32_t>::max());
}

LockFreeAddressHashSet::~LockFreeAddressHashSet() {
  for (subtle::AtomicWord bucket : buckets_) {
    Node* node = reinterpret_cast<Node*>(bucket);
    while (node) {
      Node* next = reinterpret_cast<Node*>(node->next);
      delete node;
      node = next;
    }
  }
}

void LockFreeAddressHashSet::Insert(void* key) {
  // TODO(alph): Replace with DCHECK.
  CHECK(key != nullptr);
  CHECK(!Contains(key));
  subtle::NoBarrier_AtomicIncrement(&size_, 1);
  uint32_t h = Hash(key);
  subtle::AtomicWord* bucket_ptr = &buckets_[h & bucket_mask_];
  Node* node = reinterpret_cast<Node*>(subtle::NoBarrier_Load(bucket_ptr));
  // First iterate over the bucket nodes and try to reuse an empty one if found.
  for (; node != nullptr; node = next_node(node)) {
    if (subtle::NoBarrier_CompareAndSwap(
            &node->key, 0, reinterpret_cast<subtle::AtomicWord>(key)) == 0) {
      return;
    }
  }
  DCHECK(node == nullptr);
  // There are no empty nodes to reuse in the bucket.
  // Create a new node and prepend it to the list.
  Node* new_node = new Node(key);
  subtle::AtomicWord current_head = subtle::NoBarrier_Load(bucket_ptr);
  subtle::AtomicWord expected_head;
  do {
    subtle::NoBarrier_Store(&new_node->next, current_head);
    expected_head = current_head;
    current_head = subtle::Release_CompareAndSwap(
        bucket_ptr, current_head,
        reinterpret_cast<subtle::AtomicWord>(new_node));
  } while (current_head != expected_head);
}

void LockFreeAddressHashSet::Copy(const LockFreeAddressHashSet& other) {
  DCHECK_EQ(0u, size());
  for (subtle::AtomicWord bucket : other.buckets_) {
    for (Node* node = reinterpret_cast<Node*>(bucket); node;
         node = next_node(node)) {
      subtle::AtomicWord k = subtle::NoBarrier_Load(&node->key);
      if (k)
        Insert(reinterpret_cast<void*>(k));
    }
  }
}

}  // namespace base
