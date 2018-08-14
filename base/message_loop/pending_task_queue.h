// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_PENDING_TASK_QUEUE_H_
#define BASE_MESSAGE_LOOP_PENDING_TASK_QUEUE_H_

#include "base/macros.h"
#include "base/pending_task.h"
#include "base/sequence_checker.h"

namespace base {
namespace internal {

// Provides storage for tasks deferred by MessageLoop via DelayedQueue and
// DeferredQueue.
class PendingTaskQueue {
 public:
  // Provides a read-write task queue.
  class Queue {
   public:
    Queue() = default;
    virtual ~Queue() = default;

    // Returns the next task. HasTasks() is assumed to be true.
    virtual const PendingTask& Peek() = 0;

    // Removes and returns the next task. HasTasks() is assumed to be true.
    virtual PendingTask Pop() = 0;

    // Whether this queue has tasks.
    virtual bool HasTasks() = 0;

    // Removes all tasks.
    virtual void Clear() = 0;

    // Adds the task to the end of the queue.
    virtual void Push(PendingTask pending_task) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Queue);
  };

  PendingTaskQueue();
  ~PendingTaskQueue();

  Queue& delayed_tasks() { return delayed_tasks_; }

  Queue& deferred_tasks() { return deferred_tasks_; }

  bool HasPendingHighResolutionTasks() const {
    return delayed_tasks_.HasPendingHighResolutionTasks();
  }

  // Reports UMA metrics about its queues before the MessageLoop goes to sleep
  // per being idle.
  void ReportMetricsOnIdle() const;

 private:
  // The queue for holding tasks that should be run later and sorted by expected
  // run time.
  class DelayedQueue : public Queue {
   public:
    DelayedQueue();
    ~DelayedQueue() override;

    // Queue:
    const PendingTask& Peek() override;
    PendingTask Pop() override;
    // Whether this queue has tasks after sweeping the cancelled ones in front.
    bool HasTasks() override;
    void Clear() override;
    void Push(PendingTask pending_task) override;

    size_t Size() const;
    bool HasPendingHighResolutionTasks() const {
      DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
      return pending_high_res_tasks_ > 0;
    }

   private:
    DelayedTaskQueue queue_;

    // Number of high resolution tasks in |queue_|.
    int pending_high_res_tasks_ = 0;

    SEQUENCE_CHECKER(sequence_checker_);

    DISALLOW_COPY_AND_ASSIGN(DelayedQueue);
  };

  // The queue for holding tasks that couldn't be run while the MessageLoop was
  // nested. These are generally processed during the idle stage.
  class DeferredQueue : public Queue {
   public:
    DeferredQueue();
    ~DeferredQueue() override;

    // Queue:
    const PendingTask& Peek() override;
    PendingTask Pop() override;
    bool HasTasks() override;
    void Clear() override;
    void Push(PendingTask pending_task) override;

   private:
    TaskQueue queue_;

    SEQUENCE_CHECKER(sequence_checker_);

    DISALLOW_COPY_AND_ASSIGN(DeferredQueue);
  };

  DelayedQueue delayed_tasks_;
  DeferredQueue deferred_tasks_;

  DISALLOW_COPY_AND_ASSIGN(PendingTaskQueue);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_MESSAGE_LOOP_PENDING_TASK_QUEUE_H_
