// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_SEQUENCE_H_
#define BASE_TASK_TASK_SCHEDULER_SEQUENCE_H_

#include <stddef.h>

#include "base/base_export.h"
#include "base/containers/queue.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/optional.h"
#include "base/sequence_token.h"
#include "base/task/common/intrusive_heap.h"
#include "base/task/task_scheduler/scheduler_lock.h"
#include "base/task/task_scheduler/scheduler_parallel_task_runner.h"
#include "base/task/task_scheduler/sequence_sort_key.h"
#include "base/task/task_scheduler/task.h"
#include "base/task/task_traits.h"
#include "base/threading/sequence_local_storage_map.h"

namespace base {
namespace internal {

// A Sequence holds slots each containing up to a single Task that must be
// executed in posting order.
//
// In comments below, an "empty Sequence" is a Sequence with no slot.
//
// Note: there is a known refcounted-ownership cycle in the Scheduler
// architecture: Sequence -> Task -> TaskRunner -> Sequence -> ...
// This is okay so long as the other owners of Sequence (PriorityQueue and
// SchedulerWorker in alternation and
// SchedulerWorkerPoolImpl::SchedulerWorkerDelegateImpl::GetWork()
// temporarily) keep running it (and taking Tasks from it as a result). A
// dangling reference cycle would only occur should they release their reference
// to it while it's not empty. In other words, it is only correct for them to
// release it after PopTask() returns false to indicate it was made empty by
// that call (in which case the next PushTask() will return true to indicate to
// the caller that the Sequence should be re-enqueued for execution).
//
// This class is thread-safe.
class BASE_EXPORT Sequence : public RefCountedThreadSafe<Sequence> {
 public:
  // A Transaction can perform multiple operations atomically on a
  // Sequence. While a Transaction is alive, it is guaranteed that nothing
  // else will access the Sequence; the Sequence's lock is held for the
  // lifetime of the Transaction.
  class BASE_EXPORT Transaction {
   public:
    Transaction(Transaction&& other);
    ~Transaction();

    // Adds |task| in a new slot at the end of the Sequence. Returns true if the
    // Sequence was empty before this operation.
    bool PushTask(Task task);

    // Transfers ownership of the Task in the front slot of the Sequence to the
    // caller. The front slot of the Sequence will be nullptr and remain until
    // Pop(). Cannot be called on an empty Sequence or a Sequence whose front
    // slot is already nullptr.
    //
    // Because this method cannot be called on an empty Sequence, the returned
    // Optional<Task> is never nullptr. An Optional is used in preparation for
    // the merge between TaskScheduler and TaskQueueManager (in Blink).
    // https://crbug.com/783309
    Optional<Task> TakeTask();

    // Removes the front slot of the Sequence. The front slot must have been
    // emptied by TakeTask() before this is called. Cannot be called on an empty
    // Sequence. Returns true if the Sequence is empty after this operation.
    bool Pop();

    // Returns a SequenceSortKey representing the priority of the Sequence.
    // Cannot be called on an empty Sequence.
    SequenceSortKey GetSortKey() const;

    bool IsEmpty() const;

    // Sets Sequence priority to |priority|.
    void UpdatePriority(TaskPriority priority);

    // Returns the traits of all Tasks in the Sequence.
    TaskTraits traits() const { return sequence_->traits_; }

    Sequence* sequence() const { return sequence_; }

   private:
    friend class Sequence;

    explicit Transaction(scoped_refptr<Sequence> sequence);

    Sequence* sequence_;

    DISALLOW_COPY_AND_ASSIGN(Transaction);
  };

  // |traits| is metadata that applies to all Tasks in the Sequence.
  // |scheduler_parallel_task_runner| is a reference to the
  // SchedulerParallelTaskRunner that created this Sequence, if any.
  Sequence(const TaskTraits& traits,
           scoped_refptr<SchedulerParallelTaskRunner>
               scheduler_parallel_task_runner = nullptr);

  // Begins a Transaction. This method cannot be called on a thread which has an
  // active Sequence::Transaction.
  Transaction BeginTransaction();

  // Support for IntrusiveHeap.
  void SetHeapHandle(const HeapHandle& handle);
  void ClearHeapHandle();
  HeapHandle heap_handle() const { return heap_handle_; }

  // Returns a token that uniquely identifies this Sequence.
  const SequenceToken& token() const { return token_; }

  SequenceLocalStorageMap* sequence_local_storage() {
    return &sequence_local_storage_;
  }

  // Returns the shutdown behavior of all Tasks in the Sequence. Can be
  // accessed without a Transaction because it is never mutated.
  TaskShutdownBehavior shutdown_behavior() const {
    return traits_.shutdown_behavior();
  }

 private:
  friend class RefCountedThreadSafe<Sequence>;
  ~Sequence();

  const SequenceToken token_ = SequenceToken::Create();

  // Synchronizes access to all members.
  mutable SchedulerLock lock_{UniversalPredecessor()};

  // Queue of tasks to execute.
  base::queue<Task> queue_;

  // Holds data stored through the SequenceLocalStorageSlot API.
  SequenceLocalStorageMap sequence_local_storage_;

  // The TaskTraits of all Tasks in the Sequence.
  TaskTraits traits_;

  // A reference to the SchedulerParallelTaskRunner that created this Sequence,
  // if any. Used to remove Sequence from the TaskRunner's list of Sequence
  // references when Sequence is deleted.
  const scoped_refptr<SchedulerParallelTaskRunner>
      scheduler_parallel_task_runner_;

  // The Sequence's position in its current PriorityQueue. Access is protected
  // by the PriorityQueue's lock.
  HeapHandle heap_handle_;

  DISALLOW_COPY_AND_ASSIGN(Sequence);
};

struct BASE_EXPORT SequenceAndTransaction {
  scoped_refptr<Sequence> sequence;
  Sequence::Transaction transaction;
  SequenceAndTransaction(scoped_refptr<Sequence> sequence_in,
                         Sequence::Transaction transaction_in);
  SequenceAndTransaction(SequenceAndTransaction&& other);
  static SequenceAndTransaction FromSequence(scoped_refptr<Sequence> sequence);
  ~SequenceAndTransaction();
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_SEQUENCE_H_
