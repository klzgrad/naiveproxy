// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_INCOMING_TASK_QUEUE_H_
#define BASE_MESSAGE_LOOP_INCOMING_TASK_QUEUE_H_

#include "base/base_export.h"
#include "base/callback.h"
#include "base/debug/task_annotator.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/pending_task.h"
#include "base/sequence_checker.h"
#include "base/synchronization/lock.h"
#include "base/time/time.h"

namespace base {

class MessageLoop;
class PostTaskTest;

namespace internal {

// Implements a queue of tasks posted to the message loop running on the current
// thread. This class takes care of synchronizing posting tasks from different
// threads and together with MessageLoop ensures clean shutdown.
class BASE_EXPORT IncomingTaskQueue
    : public RefCountedThreadSafe<IncomingTaskQueue> {
 public:
  // Provides a read and remove only view into a task queue.
  class ReadAndRemoveOnlyQueue {
   public:
    ReadAndRemoveOnlyQueue() = default;
    virtual ~ReadAndRemoveOnlyQueue() = default;

    // Returns the next task. HasTasks() is assumed to be true.
    virtual const PendingTask& Peek() = 0;

    // Removes and returns the next task. HasTasks() is assumed to be true.
    virtual PendingTask Pop() = 0;

    // Whether this queue has tasks.
    virtual bool HasTasks() = 0;

    // Removes all tasks.
    virtual void Clear() = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(ReadAndRemoveOnlyQueue);
  };

  // Provides a read-write task queue.
  class Queue : public ReadAndRemoveOnlyQueue {
   public:
    Queue() = default;
    ~Queue() override = default;

    // Adds the task to the end of the queue.
    virtual void Push(PendingTask pending_task) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Queue);
  };

  explicit IncomingTaskQueue(MessageLoop* message_loop);

  // Appends a task to the incoming queue. Posting of all tasks is routed though
  // AddToIncomingQueue() or TryAddToIncomingQueue() to make sure that posting
  // task is properly synchronized between different threads.
  //
  // Returns true if the task was successfully added to the queue, otherwise
  // returns false. In all cases, the ownership of |task| is transferred to the
  // called method.
  bool AddToIncomingQueue(const Location& from_here,
                          OnceClosure task,
                          TimeDelta delay,
                          Nestable nestable);

  // Disconnects |this| from the parent message loop.
  void WillDestroyCurrentMessageLoop();

  // This should be called when the message loop becomes ready for
  // scheduling work.
  void StartScheduling();

  // Runs |pending_task|.
  void RunTask(PendingTask* pending_task);

  ReadAndRemoveOnlyQueue& triage_tasks() { return triage_tasks_; }

  Queue& delayed_tasks() { return delayed_tasks_; }

  Queue& deferred_tasks() { return deferred_tasks_; }

  bool HasPendingHighResolutionTasks() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return pending_high_res_tasks_ > 0;
  }

 private:
  friend class base::PostTaskTest;
  friend class RefCountedThreadSafe<IncomingTaskQueue>;

  // These queues below support the previous MessageLoop behavior of
  // maintaining three queue queues to process tasks:
  //
  // TriageQueue
  // The first queue to receive all tasks for the processing sequence (when
  // reloading from the thread-safe |incoming_queue_|). Tasks are generally
  // either dispatched immediately or sent to the queues below.
  //
  // DelayedQueue
  // The queue for holding tasks that should be run later and sorted by expected
  // run time.
  //
  // DeferredQueue
  // The queue for holding tasks that couldn't be run while the MessageLoop was
  // nested. These are generally processed during the idle stage.
  //
  // Many of these do not share implementations even though they look like they
  // could because of small quirks (reloading semantics) or differing underlying
  // data strucutre (TaskQueue vs DelayedTaskQueue).

  // The starting point for all tasks on the sequence processing the tasks.
  class TriageQueue : public ReadAndRemoveOnlyQueue {
   public:
    TriageQueue(IncomingTaskQueue* outer);
    ~TriageQueue() override;

    // ReadAndRemoveOnlyQueue:
    // In general, the methods below will attempt to reload from the incoming
    // queue if the queue itself is empty except for Clear(). See Clear() for
    // why it doesn't reload.
    const PendingTask& Peek() override;
    PendingTask Pop() override;
    // Whether this queue has tasks after reloading from the incoming queue.
    bool HasTasks() override;
    void Clear() override;

   private:
    void ReloadFromIncomingQueueIfEmpty();

    IncomingTaskQueue* const outer_;
    TaskQueue queue_;

    DISALLOW_COPY_AND_ASSIGN(TriageQueue);
  };

  class DelayedQueue : public Queue {
   public:
    DelayedQueue(IncomingTaskQueue* outer);
    ~DelayedQueue() override;

    // Queue:
    const PendingTask& Peek() override;
    PendingTask Pop() override;
    // Whether this queue has tasks after sweeping the cancelled ones in front.
    bool HasTasks() override;
    void Clear() override;
    void Push(PendingTask pending_task) override;

   private:
    IncomingTaskQueue* const outer_;
    DelayedTaskQueue queue_;

    DISALLOW_COPY_AND_ASSIGN(DelayedQueue);
  };

  class DeferredQueue : public Queue {
   public:
    DeferredQueue(IncomingTaskQueue* outer);
    ~DeferredQueue() override;

    // Queue:
    const PendingTask& Peek() override;
    PendingTask Pop() override;
    bool HasTasks() override;
    void Clear() override;
    void Push(PendingTask pending_task) override;

   private:
    IncomingTaskQueue* const outer_;
    TaskQueue queue_;

    DISALLOW_COPY_AND_ASSIGN(DeferredQueue);
  };

  virtual ~IncomingTaskQueue();

  // Adds a task to |incoming_queue_|. The caller retains ownership of
  // |pending_task|, but this function will reset the value of
  // |pending_task->task|. This is needed to ensure that the posting call stack
  // does not retain |pending_task->task| beyond this function call.
  bool PostPendingTask(PendingTask* pending_task);

  // Does the real work of posting a pending task. Returns true if the caller
  // should call ScheduleWork() on the message loop.
  bool PostPendingTaskLockRequired(PendingTask* pending_task);

  // Loads tasks from the |incoming_queue_| into |*work_queue|. Must be called
  // from the sequence processing the tasks. Returns the number of tasks that
  // require high resolution timers in |work_queue|.
  int ReloadWorkQueue(TaskQueue* work_queue);

  // Checks calls made only on the MessageLoop thread.
  SEQUENCE_CHECKER(sequence_checker_);

  debug::TaskAnnotator task_annotator_;

  // True if we always need to call ScheduleWork when receiving a new task, even
  // if the incoming queue was not empty.
  const bool always_schedule_work_;

  // Queue for initial triaging of tasks on the |sequence_checker_| sequence.
  TriageQueue triage_tasks_;

  // Queue for delayed tasks on the |sequence_checker_| sequence.
  DelayedQueue delayed_tasks_;

  // Queue for non-nestable deferred tasks on the |sequence_checker_| sequence.
  DeferredQueue deferred_tasks_;

  // Number of high resolution tasks in the sequence affine queues above.
  int pending_high_res_tasks_ = 0;

  // Lock that serializes |message_loop_->ScheduleWork()| calls as well as
  // prevents |message_loop_| from being made nullptr during such a call.
  base::Lock message_loop_lock_;

  // Points to the message loop that owns |this|.
  MessageLoop* message_loop_;

  // Synchronizes access to all members below this line.
  base::Lock incoming_queue_lock_;

  // Number of tasks that require high resolution timing. This value is kept
  // so that ReloadWorkQueue() completes in constant time.
  int high_res_task_count_ = 0;

  // An incoming queue of tasks that are acquired under a mutex for processing
  // on this instance's thread. These tasks have not yet been been pushed to
  // |triage_tasks_|.
  TaskQueue incoming_queue_;

  // True if new tasks should be accepted.
  bool accept_new_tasks_ = true;

  // The next sequence number to use for delayed tasks.
  int next_sequence_num_ = 0;

  // True if our message loop has already been scheduled and does not need to be
  // scheduled again until an empty reload occurs.
  bool message_loop_scheduled_ = false;

  // False until StartScheduling() is called.
  bool is_ready_for_scheduling_ = false;

  DISALLOW_COPY_AND_ASSIGN(IncomingTaskQueue);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_MESSAGE_LOOP_INCOMING_TASK_QUEUE_H_
