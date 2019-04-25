// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_PRIORITY_QUEUE_H_
#define BASE_TASK_TASK_SCHEDULER_PRIORITY_QUEUE_H_

#include <memory>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/task/common/intrusive_heap.h"
#include "base/task/task_scheduler/scheduler_lock.h"
#include "base/task/task_scheduler/sequence.h"
#include "base/task/task_scheduler/sequence_sort_key.h"

namespace base {
namespace internal {

// A PriorityQueue holds Sequences of Tasks. This class is not thread-safe
// (requires external synchronization).
class BASE_EXPORT PriorityQueue {
 public:
  PriorityQueue();
  ~PriorityQueue();

  // Inserts |sequence| in the PriorityQueue with |sequence_sort_key|. Note:
  // |sequence_sort_key| is required as a parameter instead of being extracted
  // from |sequence| in Push() to avoid this Transaction having a lock
  // interdependency with |sequence|.
  void Push(scoped_refptr<Sequence> sequence,
            const SequenceSortKey& sequence_sort_key);

  // Returns a reference to the SequenceSortKey representing the priority of
  // the highest pending task in this PriorityQueue. The reference becomes
  // invalid the next time that this PriorityQueue is modified.
  // Cannot be called on an empty PriorityQueue.
  const SequenceSortKey& PeekSortKey() const;

  // Removes and returns the highest priority Sequence in this PriorityQueue.
  // Cannot be called on an empty PriorityQueue.
  scoped_refptr<Sequence> PopSequence();

  // Removes |sequence| from the PriorityQueue. Returns true if successful, or
  // false if |sequence| is not currently in the PriorityQueue or the
  // PriorityQueue is empty.
  bool RemoveSequence(scoped_refptr<Sequence> sequence);

  // Updates the sort key of the Sequence in |sequence_and_transaction| to
  // match its current traits. No-ops if the Sequence is not in the
  // PriorityQueue or the PriorityQueue is empty.
  void UpdateSortKey(SequenceAndTransaction sequence_and_transaction);

  // Returns true if the PriorityQueue is empty.
  bool IsEmpty() const;

  // Returns the number of Sequences in the PriorityQueue.
  size_t Size() const;

  // Returns the number of Sequences with |priority|.
  size_t GetNumSequencesWithPriority(TaskPriority priority) const {
    return num_sequences_per_priority_[static_cast<int>(priority)];
  }

  // Set the PriorityQueue to empty all its Sequences of Tasks when it is
  // destroyed; needed to prevent memory leaks caused by a reference cycle
  // (Sequence -> Task -> TaskRunner -> Sequence...) during test teardown.
  void EnableFlushSequencesOnDestroyForTesting();

 private:
  // A class combining a Sequence and the SequenceSortKey that determines its
  // position in a PriorityQueue.
  class SequenceAndSortKey;

  using ContainerType = IntrusiveHeap<SequenceAndSortKey>;

  void DecrementNumSequencesForPriority(TaskPriority priority);
  void IncrementNumSequencesForPriority(TaskPriority priority);

  ContainerType container_;

  size_t num_sequences_per_priority_[static_cast<int>(TaskPriority::HIGHEST) +
                                     1] = {};

  // Should only be enabled by EnableFlushSequencesOnDestroyForTesting().
  bool is_flush_sequences_on_destroy_enabled_ = false;

  DISALLOW_COPY_AND_ASSIGN(PriorityQueue);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_PRIORITY_QUEUE_H_
