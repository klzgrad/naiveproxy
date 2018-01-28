// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_TASK_TRACKER_H_
#define BASE_TASK_SCHEDULER_TASK_TRACKER_H_

#include <functional>
#include <memory>
#include <queue>

#include "base/atomicops.h"
#include "base/base_export.h"
#include "base/callback_forward.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/metrics/histogram_base.h"
#include "base/synchronization/waitable_event.h"
#include "base/task_scheduler/can_schedule_sequence_observer.h"
#include "base/task_scheduler/scheduler_lock.h"
#include "base/task_scheduler/sequence.h"
#include "base/task_scheduler/task.h"
#include "base/task_scheduler/task_traits.h"

namespace base {

class ConditionVariable;
class HistogramBase;

namespace internal {

// TaskTracker enforces policies that determines whether:
// - A task can be added to a sequence (WillPostTask).
// - A sequence can be scheduled (WillScheduleSequence).
// - The next task in a scheduled sequence can run (RunNextTask).
// TaskTracker also sets up the environment to run a task (RunNextTask) and
// records metrics and trace events. This class is thread-safe.
//
// Life of a sequence:
// (possible states: IDLE, PREEMPTED, SCHEDULED, RUNNING)
//
//                            Create a sequence
//                                   |
//  ------------------------> Sequence is IDLE
//  |                                |
//  |                     Add a task to the sequence
//  |            (allowed by TaskTracker::WillPostTask)
//  |                                |
//  |               TaskTracker:WillScheduleSequence
//  |           _____________________|_____________________
//  |           |                                          |
//  |    Returns true                                Returns false
//  |           |                                          |
//  |           |                                Sequence is PREEMPTED <----
//  |           |                                          |               |
//  |           |                            Eventually,                   |
//  |           |                            CanScheduleSequenceObserver   |
//  |           |                            is notified that the          |
//  |           |                            sequence can be scheduled.    |
//  |           |__________________________________________|               |
//  |                               |                                      |
//  |                   (*) Sequence is SCHEDULED                          |
//  |                               |                                      |
//  |                A thread is ready to run the next                     |
//  |                      task in the sequence                            |
//  |                               |                                      |
//  |                   TaskTracker::RunNextTask                           |
//  |                A task from the sequence is run                       |
//  |                      Sequence is RUNNING                             |
//  |                               |                                      |
//  |         ______________________|____                                  |
//  |         |                          |                                 |
//  |   Sequence is empty      Sequence has more tasks                     |
//  |_________|             _____________|_______________                  |
//                          |                            |                 |
//                   Sequence can be            Sequence cannot be         |
//                   scheduled                  scheduled at this          |
//                          |                   moment                     |
//                   Go back to (*)                      |_________________|
//
//
// Note: A background task is a task posted with TaskPriority::BACKGROUND. A
// foreground task is a task posted with TaskPriority::USER_VISIBLE or
// TaskPriority::USER_BLOCKING.
class BASE_EXPORT TaskTracker {
 public:
  // |max_num_scheduled_background_sequences| is the maximum number of
  // background sequences that be scheduled concurrently.
  TaskTracker(int max_num_scheduled_background_sequences =
                  std::numeric_limits<int>::max());
  virtual ~TaskTracker();

  // Synchronously shuts down the scheduler. Once this is called, only tasks
  // posted with the BLOCK_SHUTDOWN behavior will be run. Returns when:
  // - All SKIP_ON_SHUTDOWN tasks that were already running have completed their
  //   execution.
  // - All posted BLOCK_SHUTDOWN tasks have completed their execution.
  // CONTINUE_ON_SHUTDOWN tasks still may be running after Shutdown returns.
  // This can only be called once.
  void Shutdown();

  // Waits until there are no pending undelayed tasks. May be called in tests
  // to validate that a condition is met after all undelayed tasks have run.
  //
  // Does not wait for delayed tasks. Waits for undelayed tasks posted from
  // other threads during the call. Returns immediately when shutdown completes.
  void Flush();

  // Informs this TaskTracker that |task| is about to be posted. Returns true if
  // this operation is allowed (|task| should be posted if-and-only-if it is).
  bool WillPostTask(const Task* task);

  // Informs this TaskTracker that |sequence| is about to be scheduled. If this
  // returns |sequence|, it is expected that RunNextTask() will soon be called
  // with |sequence| as argument. Otherwise, RunNextTask() must not be called
  // with |sequence| as argument until |observer| is notified that |sequence|
  // can be scheduled (the caller doesn't need to keep a pointer to |sequence|;
  // it will be included in the notification to |observer|). WillPostTask() must
  // have allowed the task in front of |sequence| to be posted before this is
  // called. |observer| is only required if the priority of |sequence| is
  // TaskPriority::BACKGROUND
  scoped_refptr<Sequence> WillScheduleSequence(
      scoped_refptr<Sequence> sequence,
      CanScheduleSequenceObserver* observer);

  // Runs the next task in |sequence| unless the current shutdown state prevents
  // that. Then, pops the task from |sequence| (even if it didn't run). Returns
  // |sequence| if it can be rescheduled immediately. If |sequence| is non-empty
  // after popping a task from it but it can't be rescheduled immediately, it
  // will be handed back to |observer| when it can be rescheduled.
  // WillPostTask() must have allowed the task in front of |sequence| to be
  // posted before this is called. Also, WillScheduleSequence(), RunNextTask()
  // or CanScheduleSequenceObserver::OnCanScheduleSequence() must have allowed
  // |sequence| to be (re)scheduled.
  scoped_refptr<Sequence> RunNextTask(scoped_refptr<Sequence> sequence,
                                      CanScheduleSequenceObserver* observer);

  // Returns true once shutdown has started (Shutdown() has been called but
  // might not have returned). Note: sequential consistency with the thread
  // calling Shutdown() (or SetHasShutdownStartedForTesting()) isn't guaranteed
  // by this call.
  bool HasShutdownStarted() const;

  // Returns true if shutdown has completed (Shutdown() has returned).
  bool IsShutdownComplete() const;

  // Causes HasShutdownStarted() to return true. Unlike when Shutdown() returns,
  // IsShutdownComplete() won't return true after this returns. Shutdown()
  // cannot be called after this.
  void SetHasShutdownStartedForTesting();

 protected:
  // Runs and deletes |task| if |can_run_task| is true. Otherwise, just deletes
  // |task|. |task| is always deleted in the environment where it runs or would
  // have run. |sequence| is the sequence from which |task| was extracted. An
  // override is expected to call its parent's implementation but is free to
  // perform extra work before and after doing so.
  virtual void RunOrSkipTask(std::unique_ptr<Task> task,
                             Sequence* sequence,
                             bool can_run_task);

#if DCHECK_IS_ON()
  // Returns true if this context should be exempt from blocking shutdown
  // DCHECKs.
  // TODO(robliao): Remove when http://crbug.com/698140 is fixed.
  virtual bool IsPostingBlockShutdownTaskAfterShutdownAllowed();
#endif

  // Called at the very end of RunNextTask() after the completion of all task
  // metrics accounting.
  virtual void OnRunNextTaskCompleted() {}

  // Returns the number of undelayed tasks that haven't completed their
  // execution.
  int GetNumPendingUndelayedTasksForTesting() const;

 private:
  class State;
  struct PreemptedBackgroundSequence;

  void PerformShutdown();

  // Called before WillPostTask() informs the tracing system that a task has
  // been posted. Updates |num_tasks_blocking_shutdown_| if necessary and
  // returns true if the current shutdown state allows the task to be posted.
  bool BeforePostTask(TaskShutdownBehavior shutdown_behavior);

  // Called before a task with |shutdown_behavior| is run by RunTask(). Updates
  // |num_tasks_blocking_shutdown_| if necessary and returns true if the current
  // shutdown state allows the task to be run.
  bool BeforeRunTask(TaskShutdownBehavior shutdown_behavior);

  // Called after a task with |shutdown_behavior| has been run by RunTask().
  // Updates |num_tasks_blocking_shutdown_| and signals |shutdown_cv_| if
  // necessary.
  void AfterRunTask(TaskShutdownBehavior shutdown_behavior);

  // Called when the number of tasks blocking shutdown becomes zero after
  // shutdown has started.
  void OnBlockingShutdownTasksComplete();

  // Decrements the number of pending undelayed tasks and signals |flush_cv_| if
  // it reaches zero.
  void DecrementNumPendingUndelayedTasks();

  // To be called after running a background task from |just_ran_sequence|.
  // Performs the following actions:
  //  - If |just_ran_sequence| is non-null:
  //    - returns it if it should be rescheduled by the caller of RunNextTask(),
  //      i.e. its next task is set to run earlier than the earliest currently
  //      preempted sequence.
  //    - Otherwise |just_ran_sequence| is preempted and the next preempted
  //      sequence is scheduled (|observer| will be notified when
  //      |just_ran_sequence| should be scheduled again).
  //  - If |just_ran_sequence| is null (RunNextTask() just popped the last task
  //    from it):
  //    - the next preempeted sequence (if any) is scheduled.
  //  - In all cases: adjusts the number of scheduled background sequences
  //    accordingly.
  scoped_refptr<Sequence> ManageBackgroundSequencesAfterRunningTask(
      scoped_refptr<Sequence> just_ran_sequence,
      CanScheduleSequenceObserver* observer);

  // Records the TaskScheduler.TaskLatency.[task priority].[may block] histogram
  // for |task|.
  void RecordTaskLatencyHistogram(Task* task);

  // Number of tasks blocking shutdown and boolean indicating whether shutdown
  // has started.
  const std::unique_ptr<State> state_;

  // Number of undelayed tasks that haven't completed their execution. Is
  // decremented with a memory barrier after a task runs. Is accessed with an
  // acquire memory barrier in Flush(). The memory barriers ensure that the
  // memory written by flushed tasks is visible when Flush() returns.
  subtle::Atomic32 num_pending_undelayed_tasks_ = 0;

  // Lock associated with |flush_cv_|. Partially synchronizes access to
  // |num_pending_undelayed_tasks_|. Full synchronization isn't needed because
  // it's atomic, but synchronization is needed to coordinate waking and
  // sleeping at the right time.
  mutable SchedulerLock flush_lock_;

  // Signaled when |num_pending_undelayed_tasks_| is zero or when shutdown
  // completes.
  const std::unique_ptr<ConditionVariable> flush_cv_;

  // Synchronizes access to shutdown related members below.
  mutable SchedulerLock shutdown_lock_;

  // Event instantiated when shutdown starts and signaled when shutdown
  // completes.
  std::unique_ptr<WaitableEvent> shutdown_event_;

  // Maximum number of background sequences that can that be scheduled
  // concurrently.
  const int max_num_scheduled_background_sequences_;

  // Synchronizes accesses to |preempted_background_sequences_| and
  // |num_scheduled_background_sequences_|.
  SchedulerLock background_lock_;

  // A priority queue of sequences that are waiting to be scheduled. Use
  // std::greater so that the sequence which contains the task that has been
  // posted the earliest is on top of the priority queue.
  std::priority_queue<PreemptedBackgroundSequence,
                      std::vector<PreemptedBackgroundSequence>,
                      std::greater<PreemptedBackgroundSequence>>
      preempted_background_sequences_;

  // Number of currently scheduled background sequences.
  int num_scheduled_background_sequences_ = 0;

  // TaskScheduler.TaskLatency.[task priority].[may block] histograms. The first
  // index is a TaskPriority. The second index is 0 for non-blocking tasks, 1
  // for blocking tasks. Intentionally leaked.
  HistogramBase* const
      task_latency_histograms_[static_cast<int>(TaskPriority::HIGHEST) + 1][2];

  // Number of BLOCK_SHUTDOWN tasks posted during shutdown.
  HistogramBase::Sample num_block_shutdown_tasks_posted_during_shutdown_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TaskTracker);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_TASK_TRACKER_H_
