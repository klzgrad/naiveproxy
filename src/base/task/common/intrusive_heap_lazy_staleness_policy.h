// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_COMMON_INTRUSIVE_HEAP_LAZY_STALENESS_POLICY_H_
#define BASE_TASK_COMMON_INTRUSIVE_HEAP_LAZY_STALENESS_POLICY_H_

#include <vector>

namespace base {
namespace internal {

// Discovers stale nodes upon bubble-up or bubble-down.
// T must implement IsStale() in order to use this policy.
template <typename T>
struct LazyStalenessPolicy {
  LazyStalenessPolicy() : num_known_stale_nodes_(0) {}

  ~LazyStalenessPolicy() = default;

  void MarkIfStale(T& t, size_t old_pos, size_t new_pos) {
    if (!t.IsStale()) {
      Unmark(new_pos);
      return;
    }

    Unmark(old_pos);
    if (!is_stale_[new_pos])
      num_known_stale_nodes_++;

    is_stale_[new_pos] = true;
  }

  void Unmark(size_t position) {
    if (position == 0 || !is_stale_[position])
      return;

    is_stale_[position] = false;
    num_known_stale_nodes_--;
  }

  void HeapResized(size_t size) { is_stale_.resize(size); }

  size_t NumKnownStaleNodes() const { return num_known_stale_nodes_; }

 private:
  friend class IntrusiveHeapLazyStalenessPolicyTest;

  size_t num_known_stale_nodes_;
  std::vector<bool> is_stale_;
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_COMMON_INTRUSIVE_HEAP_LAZY_STALENESS_POLICY_H_
