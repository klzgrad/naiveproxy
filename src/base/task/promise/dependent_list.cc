// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/promise/dependent_list.h"

#include "base/task/promise/abstract_promise.h"

namespace base {
namespace internal {

DependentList::DependentList(ConstructUnresolved) : head_(0) {}

DependentList::DependentList(ConstructResolved) : head_(kResolvedSentinel) {}

DependentList::DependentList(ConstructRejected) : head_(kRejectedSentinel) {}

DependentList::~DependentList() = default;

DependentList::Node::Node() = default;

DependentList::Node::Node(Node&& other) {
  dependent = std::move(other.dependent);
  DCHECK_EQ(other.next, nullptr);
}

DependentList::Node::~Node() = default;

DependentList::InsertResult DependentList::Insert(Node* node) {
  // This method uses std::memory_order_acquire semantics on read (the failure
  // case of compare_exchange_weak() below is a read) to ensure setting
  // |node->next| happens-after all memory modifications applied to |prev_head|
  // before it became |head_|. Conversely it uses std::memory_order_release
  // semantics on write to ensure that all memory modifications applied to
  // |node| happened-before it becomes |head_|.
  DCHECK(!node->next);
  uintptr_t prev_head = head_.load(std::memory_order_acquire);
  do {
    if (prev_head == kResolvedSentinel) {
      node->next = 0;
      return InsertResult::FAIL_PROMISE_RESOLVED;
    }

    if (prev_head == kRejectedSentinel) {
      node->next = 0;
      return InsertResult::FAIL_PROMISE_REJECTED;
    }

    if (prev_head == kCanceledSentinel) {
      node->next = 0;
      return InsertResult::FAIL_PROMISE_CANCELED;
    }

    node->next = reinterpret_cast<Node*>(prev_head);
  } while (!head_.compare_exchange_weak(
      prev_head, reinterpret_cast<uintptr_t>(node), std::memory_order_release,
      std::memory_order_acquire));
  return InsertResult::SUCCESS;
}

DependentList::Node* DependentList::ConsumeOnceForResolve() {
  // The Consume*() methods require std::memory_order_acq_rel semantics because:
  //   * Need release semantics to ensure that future calls to Insert() (which
  //     will fail) happen-after memory modifications performed prior to this
  //     Consume*().
  //   * Need acquire semantics to synchronize with the last Insert() and ensure
  //     all memory modifications applied to |head_| before the last Insert()
  //     happen-before this Consume*().
  uintptr_t prev_head = std::atomic_exchange_explicit(
      &head_, kResolvedSentinel, std::memory_order_acq_rel);
  DCHECK_NE(prev_head, kResolvedSentinel);
  DCHECK_NE(prev_head, kRejectedSentinel);
  DCHECK_NE(prev_head, kCanceledSentinel);
  return reinterpret_cast<Node*>(prev_head);
}

DependentList::Node* DependentList::ConsumeOnceForReject() {
  uintptr_t prev_head = std::atomic_exchange_explicit(
      &head_, kRejectedSentinel, std::memory_order_acq_rel);
  DCHECK_NE(prev_head, kResolvedSentinel);
  DCHECK_NE(prev_head, kRejectedSentinel);
  DCHECK_NE(prev_head, kCanceledSentinel);
  return reinterpret_cast<Node*>(prev_head);
}

DependentList::Node* DependentList::ConsumeOnceForCancel() {
  uintptr_t prev_head = std::atomic_exchange_explicit(
      &head_, kCanceledSentinel, std::memory_order_acq_rel);
  DCHECK_NE(prev_head, kResolvedSentinel);
  DCHECK_NE(prev_head, kRejectedSentinel);
  DCHECK_NE(prev_head, kCanceledSentinel);
  return reinterpret_cast<Node*>(prev_head);
}

bool DependentList::IsSettled() const {
  uintptr_t value = head_.load(std::memory_order_acquire);
  return value == kResolvedSentinel || value == kRejectedSentinel ||
         value == kCanceledSentinel;
}

bool DependentList::IsResolved() const {
  return head_.load(std::memory_order_acquire) == kResolvedSentinel;
}

bool DependentList::IsRejected() const {
  return head_.load(std::memory_order_acquire) == kRejectedSentinel;
}

bool DependentList::IsCanceled() const {
  return head_.load(std::memory_order_acquire) == kCanceledSentinel;
}

constexpr uintptr_t DependentList::kResolvedSentinel;
constexpr uintptr_t DependentList::kRejectedSentinel;
constexpr uintptr_t DependentList::kCanceledSentinel;

}  // namespace internal
}  // namespace base
