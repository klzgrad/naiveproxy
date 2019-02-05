// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MESSAGE_LOOP_MESSAGE_LOOP_IMPL_H_
#define BASE_MESSAGE_LOOP_MESSAGE_LOOP_IMPL_H_

#include <memory>
#include <queue>
#include <string>

#include "base/base_export.h"
#include "base/callback_forward.h"
#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_pump.h"
#include "base/message_loop/pending_task_queue.h"
#include "base/message_loop/timer_slack.h"
#include "base/observer_list.h"
#include "base/pending_task.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/threading/sequence_local_storage_map.h"
#include "base/threading/thread_checker.h"
#include "base/time/time.h"
#include "build/build_config.h"

namespace base {

class SequencedTaskSource;
class ThreadTaskRunnerHandle;

namespace internal {
class MessageLoopTaskRunner;
}

// A MessageLoopImpl is the implementation of the MessageLoop which provides the
// basic scheduling functionality. MessageLoopImpl is the legacy implementation,
// which is being deprecated and replaced with SequenceManager-based
// implementation (crbug.com/891670).
class BASE_EXPORT MessageLoopImpl : public MessageLoopBase,
                                    public MessagePump::Delegate,
                                    public RunLoop::Delegate {
 public:
  // Create an unbound MessageLoopImpl implementation.
  // Pump will be created by owning MessageLoop and will be passed via
  // BindToCurrentThread.
  explicit MessageLoopImpl(MessageLoopBase::Type type);

  ~MessageLoopImpl() override;

  // MessageLoopBase implementation:
  bool IsType(MessageLoopBase::Type type) const override;
  std::string GetThreadName() const override;
  void SetTaskRunner(
      scoped_refptr<SingleThreadTaskRunner> task_runner) override;
  scoped_refptr<SingleThreadTaskRunner> GetTaskRunner() override;
  void AddDestructionObserver(
      DestructionObserver* destruction_observer) override;
  void RemoveDestructionObserver(
      DestructionObserver* destruction_observer) override;
  void AddTaskObserver(TaskObserver* task_observer) override;
  void RemoveTaskObserver(TaskObserver* task_observer) override;
  void SetAddQueueTimeToTasks(bool enable) override;
  bool IsBoundToCurrentThread() const override;
  MessagePump* GetMessagePump() const override;
  bool IsIdleForTesting() override;
  void SetTaskExecutionAllowed(bool allowed) override;
  bool IsTaskExecutionAllowed() const override;
#if defined(OS_IOS) || defined(OS_ANDROID)
  void AttachToMessagePump() override;
#endif
  void SetTimerSlack(TimerSlack timer_slack) override;
  void BindToCurrentThread(std::unique_ptr<MessagePump> pump) override;
  void DeletePendingTasks() override;
  bool HasTasks() override;

  // Gets the TaskRunner associated with this message loop.
  const scoped_refptr<SingleThreadTaskRunner>& task_runner() const {
    return task_runner_;
  }

  bool IsCurrent() const;

  // Runs the specified PendingTask.
  void RunTask(PendingTask* pending_task);

  //----------------------------------------------------------------------------
 protected:
  std::unique_ptr<MessagePump> pump_;

 private:
  friend class MessageLoopCurrent;
  friend class MessageLoopCurrentForIO;
  friend class MessageLoopCurrentForUI;
  friend class MessageLoopTaskRunnerTest;
  friend class ScheduleWorkTest;
  friend class Thread;
  friend class sequence_manager::LazyThreadControllerForTest;
  friend class sequence_manager::internal::SequenceManagerImpl;
  FRIEND_TEST_ALL_PREFIXES(MessageLoopTest, DeleteUnboundLoop);

  class Controller;

  // Sets the ThreadTaskRunnerHandle for the current thread to point to the
  // task runner for this message loop.
  void SetThreadTaskRunnerHandle();

  // RunLoop::Delegate:
  void Run(bool application_tasks_allowed) override;
  void Quit() override;
  void EnsureWorkScheduled() override;

  // Called to process any delayed non-nestable tasks.
  bool ProcessNextDelayedNonNestableTask();

  // Calls RunTask or queues the pending_task on the deferred task list if it
  // cannot be run right now.  Returns true if the task was run.
  bool DeferOrRunPendingTask(PendingTask pending_task);

  // Wakes up the message pump. Thread-safe (and callers should avoid holding a
  // Lock at all cost while making this call as some platforms' priority
  // boosting features have been observed to cause the caller to get descheduled
  // : https://crbug.com/890978).
  void ScheduleWork();

  // Returns |next_run_time| capped at 1 day from |recent_time_|. This is used
  // to mitigate https://crbug.com/850450 where some platforms are unhappy with
  // delays > 100,000,000 seconds. In practice, a diagnosis metric showed that
  // no sleep > 1 hour ever completes (always interrupted by an earlier
  // MessageLoop event) and 99% of completed sleeps are the ones scheduled for
  // <= 1 second. Details @ https://crrev.com/c/1142589.
  TimeTicks CapAtOneDay(TimeTicks next_run_time);

  // MessagePump::Delegate methods:
  bool DoWork() override;
  bool DoDelayedWork(TimeTicks* next_delayed_work_time) override;
  bool DoIdleWork() override;

  const MessageLoopBase::Type type_;

#if defined(OS_WIN)
  // Tracks if we have requested high resolution timers. Its only use is to
  // turn off the high resolution timer upon loop destruction.
  bool in_high_res_mode_ = false;
#endif

  // A recent snapshot of Time::Now(), used to check delayed_work_queue_.
  TimeTicks recent_time_;

  // A boolean which prevents unintentional reentrant task execution (e.g. from
  // induced nested message loops). As such, nested message loops will only
  // process system messages (not application tasks) by default. A nested loop
  // layer must have been explicitly granted permission to be able to execute
  // application tasks. This is granted either by
  // RunLoop::Type::kNestableTasksAllowed when the loop is driven by the
  // application or by a ScopedNestableTaskAllower preceding a system call that
  // is known to generate a system-driven nested loop.
  bool task_execution_allowed_ = true;

  //  Using an ObserverList adds significant overhead. We use a raw vector and
  //  require that callers do not attempt to mutate the list during a callback.
  //  https://crbug.com/859155#c12
  std::vector<TaskObserver*> task_observers_;

  // Pointer to this MessageLoop's Controller, valid throughout this
  // MessageLoop's lifetime (until |underlying_task_runner_| is released at the
  // end of ~MessageLoop()).
  Controller* const message_loop_controller_;

  // The task runner this MessageLoop will extract its tasks from. By default,
  // it will also be bound as the ThreadTaskRunnerHandle on the current thread.
  // That default can be overridden by SetTaskRunner() but this MessageLoop will
  // nonetheless take its tasks from |underlying_task_runner_| (the overrider is
  // responsible for doing the routing). This member must be before
  // |pending_task_queue| as it must outlive it.
  const scoped_refptr<internal::MessageLoopTaskRunner> underlying_task_runner_;

  // The source of tasks for this MessageLoop. Currently this is always
  // |underlying_task_runner_|. TODO(gab): Make this customizable.
  SequencedTaskSource* const sequenced_task_source_;

  internal::PendingTaskQueue pending_task_queue_;

  // The task runner exposed by this message loop.
  scoped_refptr<SingleThreadTaskRunner> task_runner_;
  std::unique_ptr<ThreadTaskRunnerHandle> thread_task_runner_handle_;

  // Id of the thread this message loop is bound to. Initialized once when the
  // MessageLoop is bound to its thread and constant forever after.
  PlatformThreadId thread_id_ = kInvalidThreadId;

  // Holds data stored through the SequenceLocalStorageSlot API.
  internal::SequenceLocalStorageMap sequence_local_storage_map_;

  // Enables the SequenceLocalStorageSlot API within its scope.
  // Instantiated in BindToCurrentThread().
  std::unique_ptr<internal::ScopedSetSequenceLocalStorageMapForCurrentThread>
      scoped_set_sequence_local_storage_map_for_current_thread_;

  ObserverList<DestructionObserver>::Unchecked destruction_observers_;

  // Verifies that calls are made on the thread on which BindToCurrentThread()
  // was invoked.
  THREAD_CHECKER(bound_thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(MessageLoopImpl);
};

}  // namespace base

#endif  // BASE_MESSAGE_LOOP_MESSAGE_LOOP_IMPL_H_
