// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/atomic_flag_set.h"

#include <utility>

#include "base/bits.h"
#include "base/callback.h"
#include "base/logging.h"

namespace base {
namespace sequence_manager {
namespace internal {

AtomicFlagSet::AtomicFlagSet(
    scoped_refptr<AssociatedThreadId> associated_thread)
    : associated_thread_(std::move(associated_thread)) {}

AtomicFlagSet::~AtomicFlagSet() {
  DCHECK(!alloc_list_head_);
  DCHECK(!partially_free_list_head_);
}

AtomicFlagSet::AtomicFlag::AtomicFlag() = default;

AtomicFlagSet::AtomicFlag::~AtomicFlag() {
  ReleaseAtomicFlag();
}

AtomicFlagSet::AtomicFlag::AtomicFlag(AtomicFlagSet* outer,
                                      Group* element,
                                      size_t flag_bit)
    : outer_(outer), group_(element), flag_bit_(flag_bit) {}

AtomicFlagSet::AtomicFlag::AtomicFlag(AtomicFlag&& other)
    : outer_(other.outer_), group_(other.group_), flag_bit_(other.flag_bit_) {
  other.outer_ = nullptr;
  other.group_ = nullptr;
}

void AtomicFlagSet::AtomicFlag::SetActive(bool active) {
  DCHECK(group_);
  // Release semantics are required to ensure that all memory accesses made on
  // this thread happen-before any others done on the thread running the active
  // callbacks.
  if (active) {
    group_->flags.fetch_or(flag_bit_, std::memory_order_release);
  } else {
    group_->flags.fetch_and(~flag_bit_, std::memory_order_release);
  }
}

void AtomicFlagSet::AtomicFlag::ReleaseAtomicFlag() {
  if (!group_)
    return;

  DCHECK_CALLED_ON_VALID_THREAD(outer_->associated_thread_->thread_checker);
  SetActive(false);

  // If |group_| was full then add it on the partially free list.
  if (group_->IsFull())
    outer_->AddToPartiallyFreeList(group_);

  int index = Group::IndexOfFirstFlagSet(flag_bit_);
  DCHECK(!group_->flag_callbacks[index].is_null());
  group_->flag_callbacks[index] = RepeatingClosure();
  group_->allocated_flags &= ~flag_bit_;

  // If |group_| has become empty delete it.
  if (group_->IsEmpty()) {
    outer_->RemoveFromPartiallyFreeList(group_);
    outer_->RemoveFromAllocList(group_);
  }

  outer_ = nullptr;
  group_ = nullptr;
}

AtomicFlagSet::AtomicFlag AtomicFlagSet::AddFlag(RepeatingClosure callback) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  // Allocate a new Group if needed.
  if (!partially_free_list_head_) {
    AddToAllocList(std::make_unique<Group>());
    AddToPartiallyFreeList(alloc_list_head_.get());
  }

  DCHECK(partially_free_list_head_);
  Group* group = partially_free_list_head_;
  size_t first_unoccupied_index =
      static_cast<size_t>(group->FindFirstUnallocatedFlag());
  DCHECK(!group->flag_callbacks[first_unoccupied_index]);
  group->flag_callbacks[first_unoccupied_index] = std::move(callback);

  size_t flag_bit = size_t{1} << first_unoccupied_index;
  group->allocated_flags |= flag_bit;

  if (group->IsFull())
    RemoveFromPartiallyFreeList(group);

  return AtomicFlag(this, group, flag_bit);
}

void AtomicFlagSet::RunActiveCallbacks() const {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  for (Group* iter = alloc_list_head_.get(); iter; iter = iter->next_.get()) {
    // Acquire semantics are required to guarantee that all memory side-effects
    // made by other threads that were allowed to perform operations are
    // synchronized with this thread before it returns from this method.
    size_t active_flags = std::atomic_exchange_explicit(
        &iter->flags, size_t{0}, std::memory_order_acquire);
    // This is O(number of bits set).
    while (active_flags) {
      int index = Group::IndexOfFirstFlagSet(active_flags);
      // Clear the flag.
      active_flags ^= size_t{1} << index;
      iter->flag_callbacks[index].Run();
    }
  }
}

AtomicFlagSet::Group::Group() = default;

AtomicFlagSet::Group::~Group() {
  DCHECK_EQ(allocated_flags, 0u);
  DCHECK(!partially_free_list_prev_);
  DCHECK(!partially_free_list_next_);
}

bool AtomicFlagSet::Group::IsFull() const {
  return (~allocated_flags) == 0u;
}

bool AtomicFlagSet::Group::IsEmpty() const {
  return allocated_flags == 0u;
}

int AtomicFlagSet::Group::FindFirstUnallocatedFlag() const {
  size_t unallocated_flags = ~allocated_flags;
  DCHECK_NE(unallocated_flags, 0u);
  int index = IndexOfFirstFlagSet(unallocated_flags);
  DCHECK_LT(index, kNumFlags);
  return index;
}

// static
int AtomicFlagSet::Group::IndexOfFirstFlagSet(size_t flag) {
  return kNumFlags - 1 - bits::CountLeadingZeroBits(flag);
}

void AtomicFlagSet::AddToAllocList(std::unique_ptr<Group> group) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (alloc_list_head_)
    alloc_list_head_->prev_ = group.get();

  group->next_ = std::move(alloc_list_head_);
  alloc_list_head_ = std::move(group);
}

void AtomicFlagSet::RemoveFromAllocList(Group* group) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  if (group->next_)
    group->next_->prev_ = group->prev_;

  if (group->prev_) {
    group->prev_->next_ = std::move(group->next_);
  } else {
    alloc_list_head_ = std::move(group->next_);
  }
}

void AtomicFlagSet::AddToPartiallyFreeList(Group* element) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  DCHECK_NE(partially_free_list_head_, element);
  DCHECK(!element->partially_free_list_prev_);
  DCHECK(!element->partially_free_list_next_);
  if (partially_free_list_head_)
    partially_free_list_head_->partially_free_list_prev_ = element;

  element->partially_free_list_next_ = partially_free_list_head_;
  partially_free_list_head_ = element;
}

void AtomicFlagSet::RemoveFromPartiallyFreeList(Group* element) {
  DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
  DCHECK(partially_free_list_head_);
  // Check |element| is in the list.
  DCHECK(partially_free_list_head_ == element ||
         element->partially_free_list_prev_);
  if (element->partially_free_list_next_) {
    element->partially_free_list_next_->partially_free_list_prev_ =
        element->partially_free_list_prev_;
  }

  if (element->partially_free_list_prev_) {
    element->partially_free_list_prev_->partially_free_list_next_ =
        element->partially_free_list_next_;
  } else {
    partially_free_list_head_ = element->partially_free_list_next_;
  }

  element->partially_free_list_prev_ = nullptr;
  element->partially_free_list_next_ = nullptr;
}

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base
