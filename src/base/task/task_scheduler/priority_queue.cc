// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/priority_queue.h"

#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"

namespace base {
namespace internal {

// A class combining a Sequence and the SequenceSortKey that determines its
// position in a PriorityQueue. Instances are only mutable via take_sequence()
// which can only be called once and renders its instance invalid after the
// call.
class PriorityQueue::SequenceAndSortKey {
 public:
  SequenceAndSortKey() = default;
  SequenceAndSortKey(scoped_refptr<Sequence> sequence,
                     const SequenceSortKey& sort_key)
      : sequence_(std::move(sequence)), sort_key_(sort_key) {
    DCHECK(sequence_);
  }

  // Note: while |sequence_| should always be non-null post-move (i.e. we
  // shouldn't be moving an invalid SequenceAndSortKey around), there can't be
  // a DCHECK(sequence_) on moves as IntrusiveHeap moves elements on pop
  // instead of overwriting them: resulting in the move of a SequenceAndSortKey
  // with a null |sequence_| in Transaction::Pop()'s implementation.
  SequenceAndSortKey(SequenceAndSortKey&& other) = default;
  SequenceAndSortKey& operator=(SequenceAndSortKey&& other) = default;

  // Extracts |sequence_| from this object. This object is invalid after this
  // call.
  scoped_refptr<Sequence> take_sequence() {
    DCHECK(sequence_);
    sequence_->ClearHeapHandle();
    return std::move(sequence_);
  }

  // Compares this SequenceAndSortKey to |other| based on their respective
  // |sort_key_|. Required by IntrusiveHeap.
  bool operator<=(const SequenceAndSortKey& other) const {
    return sort_key_ <= other.sort_key_;
  }

  // Required by IntrusiveHeap.
  void SetHeapHandle(const HeapHandle& handle) {
    DCHECK(sequence_);
    sequence_->SetHeapHandle(handle);
  }

  // Required by IntrusiveHeap.
  void ClearHeapHandle() {
    // Ensure |sequence_| is not nullptr, which may be the case if
    // take_sequence() was called before this.
    if (sequence_) {
      sequence_->ClearHeapHandle();
    }
  }

  const Sequence* sequence() const { return sequence_.get(); }

  const SequenceSortKey& sort_key() const { return sort_key_; }

 private:
  scoped_refptr<Sequence> sequence_;
  SequenceSortKey sort_key_;

  DISALLOW_COPY_AND_ASSIGN(SequenceAndSortKey);
};

PriorityQueue::PriorityQueue() = default;

PriorityQueue::~PriorityQueue() {
  if (!is_flush_sequences_on_destroy_enabled_)
    return;

  while (!container_.empty()) {
    scoped_refptr<Sequence> sequence = PopSequence();
    Sequence::Transaction sequence_transaction(sequence->BeginTransaction());
    while (!sequence_transaction.IsEmpty()) {
      sequence_transaction.TakeTask();
      sequence_transaction.Pop();
    }
  }
}

void PriorityQueue::Push(scoped_refptr<Sequence> sequence,
                         const SequenceSortKey& sequence_sort_key) {
  container_.insert(SequenceAndSortKey(std::move(sequence), sequence_sort_key));
  IncrementNumSequencesForPriority(sequence_sort_key.priority());
}

const SequenceSortKey& PriorityQueue::PeekSortKey() const {
  DCHECK(!IsEmpty());
  return container_.Min().sort_key();
}

scoped_refptr<Sequence> PriorityQueue::PopSequence() {
  DCHECK(!IsEmpty());

  // The const_cast on Min() is okay since the SequenceAndSortKey is
  // transactionally being popped from |container_| right after and taking its
  // Sequence does not alter its sort order.
  auto& sequence_and_sort_key =
      const_cast<PriorityQueue::SequenceAndSortKey&>(container_.Min());
  DecrementNumSequencesForPriority(sequence_and_sort_key.sort_key().priority());
  scoped_refptr<Sequence> sequence = sequence_and_sort_key.take_sequence();
  container_.Pop();
  return sequence;
}

bool PriorityQueue::RemoveSequence(scoped_refptr<Sequence> sequence) {
  DCHECK(sequence);

  if (IsEmpty())
    return false;

  const HeapHandle heap_handle = sequence->heap_handle();
  if (!heap_handle.IsValid())
    return false;

  const SequenceAndSortKey& sequence_and_sort_key = container_.at(heap_handle);
  DCHECK_EQ(sequence_and_sort_key.sequence(), sequence.get());

  DecrementNumSequencesForPriority(sequence_and_sort_key.sort_key().priority());
  container_.erase(heap_handle);
  return true;
}

void PriorityQueue::UpdateSortKey(
    SequenceAndTransaction sequence_and_transaction) {
  DCHECK(sequence_and_transaction.sequence);

  if (IsEmpty())
    return;

  const HeapHandle heap_handle =
      sequence_and_transaction.sequence->heap_handle();
  if (!heap_handle.IsValid())
    return;

  auto old_sort_key = container_.at(heap_handle).sort_key();
  auto new_sort_key = sequence_and_transaction.transaction.GetSortKey();

  DecrementNumSequencesForPriority(old_sort_key.priority());
  IncrementNumSequencesForPriority(new_sort_key.priority());

  container_.ChangeKey(
      heap_handle,
      SequenceAndSortKey(std::move(sequence_and_transaction.sequence),
                         new_sort_key));
}

bool PriorityQueue::IsEmpty() const {
  return container_.empty();
}

size_t PriorityQueue::Size() const {
  return container_.size();
}

void PriorityQueue::EnableFlushSequencesOnDestroyForTesting() {
  DCHECK(!is_flush_sequences_on_destroy_enabled_);
  is_flush_sequences_on_destroy_enabled_ = true;
}

void PriorityQueue::DecrementNumSequencesForPriority(TaskPriority priority) {
  DCHECK_GT(num_sequences_per_priority_[static_cast<int>(priority)], 0U);
  --num_sequences_per_priority_[static_cast<int>(priority)];
}

void PriorityQueue::IncrementNumSequencesForPriority(TaskPriority priority) {
  ++num_sequences_per_priority_[static_cast<int>(priority)];
}

}  // namespace internal
}  // namespace base
