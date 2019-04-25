// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_SAMPLING_HEAP_PROFILER_LOCK_FREE_ADDRESS_HASH_SET_H_
#define BASE_SAMPLING_HEAP_PROFILER_LOCK_FREE_ADDRESS_HASH_SET_H_

#include <cstdint>
#include <vector>

#include "base/atomicops.h"
#include "base/logging.h"

namespace base {

// A hash set container that provides lock-free versions of
// |Insert|, |Remove|, and |Contains| operations.
// It does not support concurrent write operations |Insert| and |Remove|
// over the same key. Concurrent writes of distinct keys are ok.
// |Contains| method can be executed concurrently with other |Insert|, |Remove|,
// or |Contains| even over the same key.
// However, please note the result of concurrent execution of |Contains|
// with |Insert| or |Remove| is racy.
//
// The hash set never rehashes, so the number of buckets stays the same
// for the lifetime of the set.
//
// Internally the hashset is implemented as a vector of N buckets
// (N has to be a power of 2). Each bucket holds a single-linked list of
// nodes each corresponding to a key.
// It is not possible to really delete nodes from the list as there might
// be concurrent reads being executed over the node. The |Remove| operation
// just marks the node as empty by placing nullptr into its key field.
// Consequent |Insert| operations may reuse empty nodes when possible.
//
// The structure of the hashset for N buckets is the following:
// 0: {*}--> {key1,*}--> {key2,*}--> NULL
// 1: {*}--> NULL
// 2: {*}--> {NULL,*}--> {key3,*}--> {key4,*}--> NULL
// ...
// N-1: {*}--> {keyM,*}--> NULL
class BASE_EXPORT LockFreeAddressHashSet {
 public:
  explicit LockFreeAddressHashSet(size_t buckets_count);
  ~LockFreeAddressHashSet();

  // Checks if the |key| is in the set. Can be executed concurrently with
  // |Insert|, |Remove|, and |Contains| operations.
  bool Contains(void* key) const;

  // Removes the |key| from the set. The key must be present in the set before
  // the invocation.
  // Can be concurrent with other |Insert| and |Remove| executions, provided
  // they operate over distinct keys.
  // Concurrent |Insert| or |Remove| executions over the same key are not
  // supported.
  void Remove(void* key);

  // Inserts the |key| into the set. The key must not be present in the set
  // before the invocation.
  // Can be concurrent with other |Insert| and |Remove| executions, provided
  // they operate over distinct keys.
  // Concurrent |Insert| or |Remove| executions over the same key are not
  // supported.
  void Insert(void* key);

  // Copies contents of |other| set into the current set. The current set
  // must be empty before the call.
  // The operation cannot be executed concurrently with any other methods.
  void Copy(const LockFreeAddressHashSet& other);

  size_t buckets_count() const { return buckets_.size(); }
  size_t size() const {
    return static_cast<size_t>(subtle::NoBarrier_Load(&size_));
  }

  // Returns the average bucket utilization.
  float load_factor() const { return 1.f * size() / buckets_.size(); }

 private:
  friend class LockFreeAddressHashSetTest;

  struct Node {
    Node() : key(0), next(0) {}
    explicit Node(void* key);

    subtle::AtomicWord key;
    subtle::AtomicWord next;
  };

  static uint32_t Hash(void* key);
  Node* FindNode(void* key) const;
  Node* Bucket(void* key) const;
  static Node* next_node(Node* node) {
    return reinterpret_cast<Node*>(subtle::NoBarrier_Load(&node->next));
  }

  std::vector<subtle::AtomicWord> buckets_;
  size_t bucket_mask_;
  subtle::AtomicWord size_ = 0;
};

inline LockFreeAddressHashSet::Node::Node(void* a_key) {
  subtle::NoBarrier_Store(&key, reinterpret_cast<subtle::AtomicWord>(a_key));
  subtle::NoBarrier_Store(&next, 0);
}

inline bool LockFreeAddressHashSet::Contains(void* key) const {
  return FindNode(key) != nullptr;
}

inline void LockFreeAddressHashSet::Remove(void* key) {
  Node* node = FindNode(key);
  // TODO(alph): Replace with DCHECK.
  CHECK(node != nullptr);
  // We can never delete the node, nor detach it from the current bucket
  // as there may always be another thread currently iterating over it.
  // Instead we just mark it as empty, so |Insert| can reuse it later.
  subtle::NoBarrier_Store(&node->key, 0);
  subtle::NoBarrier_AtomicIncrement(&size_, -1);
}

inline LockFreeAddressHashSet::Node* LockFreeAddressHashSet::FindNode(
    void* key) const {
  for (Node* node = Bucket(key); node != nullptr; node = next_node(node)) {
    void* k = reinterpret_cast<void*>(subtle::NoBarrier_Load(&node->key));
    if (k == key)
      return node;
  }
  return nullptr;
}

inline LockFreeAddressHashSet::Node* LockFreeAddressHashSet::Bucket(
    void* key) const {
  // TODO(alph): Replace with DCHECK.
  CHECK(key != nullptr);
  uint32_t h = Hash(key);
  return reinterpret_cast<Node*>(
      subtle::NoBarrier_Load(&buckets_[h & bucket_mask_]));
}

// static
inline uint32_t LockFreeAddressHashSet::Hash(void* key) {
  // A simple fast hash function for addresses.
  uint64_t k = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(key));
  uint64_t random_bits = 0x4bfdb9df5a6f243bull;
  return static_cast<uint32_t>((k * random_bits) >> 32);
}

}  // namespace base

#endif  // BASE_SAMPLING_HEAP_PROFILER_LOCK_FREE_ADDRESS_HASH_SET_H_
