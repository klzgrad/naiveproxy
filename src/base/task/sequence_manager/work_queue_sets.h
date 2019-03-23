// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SEQUENCE_MANAGER_WORK_QUEUE_SETS_H_
#define BASE_TASK_SEQUENCE_MANAGER_WORK_QUEUE_SETS_H_

#include <array>
#include <map>

#include "base/base_export.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/task/common/intrusive_heap.h"
#include "base/task/sequence_manager/task_queue_impl.h"
#include "base/task/sequence_manager/work_queue.h"
#include "base/trace_event/traced_value.h"

namespace base {
namespace sequence_manager {
namespace internal {

// There is a WorkQueueSet for each scheduler priority and each WorkQueueSet
// uses a EnqueueOrderToWorkQueueMap to keep track of which queue in the set has
// the oldest task (i.e. the one that should be run next if the
// TaskQueueSelector chooses to run a task a given priority).  The reason this
// works is because std::map is a tree based associative container and all the
// values are kept in sorted order.
class BASE_EXPORT WorkQueueSets {
 public:
  class Observer {
   public:
    virtual ~Observer() {}

    virtual void WorkQueueSetBecameEmpty(size_t set_index) = 0;

    virtual void WorkQueueSetBecameNonEmpty(size_t set_index) = 0;
  };

  WorkQueueSets(const char* name, Observer* observer);
  ~WorkQueueSets();

  // O(log num queues)
  void AddQueue(WorkQueue* queue, size_t set_index);

  // O(log num queues)
  void RemoveQueue(WorkQueue* work_queue);

  // O(log num queues)
  void ChangeSetIndex(WorkQueue* queue, size_t set_index);

  // O(log num queues)
  void OnFrontTaskChanged(WorkQueue* queue);

  // O(log num queues)
  void OnTaskPushedToEmptyQueue(WorkQueue* work_queue);

  // If empty it's O(1) amortized, otherwise it's O(log num queues)
  // Assumes |work_queue| contains the lowest enqueue order in the set.
  void OnPopQueue(WorkQueue* work_queue);

  // O(log num queues)
  void OnQueueBlocked(WorkQueue* work_queue);

  // O(1)
  WorkQueue* GetOldestQueueInSet(size_t set_index) const;

  // O(1)
  WorkQueue* GetOldestQueueAndEnqueueOrderInSet(
      size_t set_index,
      EnqueueOrder* out_enqueue_order) const;

  // O(1)
  bool IsSetEmpty(size_t set_index) const;

#if DCHECK_IS_ON() || !defined(NDEBUG)
  // Note this iterates over everything in |work_queue_heaps_|.
  // It's intended for use with DCHECKS and for testing
  bool ContainsWorkQueueForTest(const WorkQueue* queue) const;
#endif

  const char* GetName() const { return name_; }

 private:
  struct OldestTaskEnqueueOrder {
    EnqueueOrder key;
    WorkQueue* value;

    bool operator<=(const OldestTaskEnqueueOrder& other) const {
      return key <= other.key;
    }

    void SetHeapHandle(base::internal::HeapHandle handle) {
      value->set_heap_handle(handle);
    }

    void ClearHeapHandle() {
      value->set_heap_handle(base::internal::HeapHandle());
    }
  };

  const char* const name_;
  Observer* const observer_;

  // For each set |work_queue_heaps_| has a queue of WorkQueue ordered by the
  // oldest task in each WorkQueue.
  std::array<base::internal::IntrusiveHeap<OldestTaskEnqueueOrder>,
             TaskQueue::kQueuePriorityCount>
      work_queue_heaps_;

  DISALLOW_COPY_AND_ASSIGN(WorkQueueSets);
};

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base

#endif  // BASE_TASK_SEQUENCE_MANAGER_WORK_QUEUE_SETS_H_
