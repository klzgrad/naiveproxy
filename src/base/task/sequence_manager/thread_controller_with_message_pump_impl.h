// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SEQUENCE_MANAGER_THREAD_CONTROLLER_WITH_MESSAGE_PUMP_IMPL_H_
#define BASE_TASK_SEQUENCE_MANAGER_THREAD_CONTROLLER_WITH_MESSAGE_PUMP_IMPL_H_

#include <memory>

#include "base/debug/task_annotator.h"
#include "base/message_loop/message_pump.h"
#include "base/run_loop.h"
#include "base/task/common/operations_controller.h"
#include "base/task/sequence_manager/associated_thread_id.h"
#include "base/task/sequence_manager/sequenced_task_source.h"
#include "base/task/sequence_manager/thread_controller.h"
#include "base/thread_annotations.h"
#include "base/threading/platform_thread.h"
#include "base/threading/sequence_local_storage_map.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"

namespace base {
namespace sequence_manager {
namespace internal {

// EXPERIMENTAL ThreadController implementation which doesn't use
// MessageLoop or a task runner to schedule their DoWork calls.
// See https://crbug.com/828835.
class BASE_EXPORT ThreadControllerWithMessagePumpImpl
    : public ThreadController,
      public MessagePump::Delegate,
      public RunLoop::Delegate,
      public RunLoop::NestingObserver {
 public:
  ThreadControllerWithMessagePumpImpl(std::unique_ptr<MessagePump> message_pump,
                                      const TickClock* time_source);
  ~ThreadControllerWithMessagePumpImpl() override;

  static std::unique_ptr<ThreadControllerWithMessagePumpImpl> CreateUnbound(
      const TickClock* time_source);

  // ThreadController implementation:
  void SetSequencedTaskSource(SequencedTaskSource* task_source) override;
  void BindToCurrentThread(MessageLoopBase* message_loop_base) override;
  void BindToCurrentThread(std::unique_ptr<MessagePump> message_pump) override;
  void SetWorkBatchSize(int work_batch_size) override;
  void WillQueueTask(PendingTask* pending_task) override;
  void ScheduleWork() override;
  void SetNextDelayedDoWork(LazyNow* lazy_now, TimeTicks run_time) override;
  void SetTimerSlack(TimerSlack timer_slack) override;
  const TickClock* GetClock() override;
  bool RunsTasksInCurrentSequence() override;
  void SetDefaultTaskRunner(
      scoped_refptr<SingleThreadTaskRunner> task_runner) override;
  scoped_refptr<SingleThreadTaskRunner> GetDefaultTaskRunner() override;
  void RestoreDefaultTaskRunner() override;
  void AddNestingObserver(RunLoop::NestingObserver* observer) override;
  void RemoveNestingObserver(RunLoop::NestingObserver* observer) override;
  const scoped_refptr<AssociatedThreadId>& GetAssociatedThread() const override;
  void SetTaskExecutionAllowed(bool allowed) override;
  bool IsTaskExecutionAllowed() const override;
  MessagePump* GetBoundMessagePump() const override;
#if defined(OS_IOS) || defined(OS_ANDROID)
  void AttachToMessagePump() override;
#endif
  bool ShouldQuitRunLoopWhenIdle() override;

  // RunLoop::NestingObserver:
  void OnBeginNestedRunLoop() override;
  void OnExitNestedRunLoop() override;

 protected:
  explicit ThreadControllerWithMessagePumpImpl(const TickClock* time_source);

  // MessagePump::Delegate implementation.
  bool DoWork() override;
  bool DoDelayedWork(TimeTicks* next_run_time) override;
  bool DoIdleWork() override;

  // RunLoop::Delegate implementation.
  void Run(bool application_tasks_allowed) override;
  void Quit() override;
  void EnsureWorkScheduled() override;

 private:
  friend class DoWorkScope;
  friend class RunScope;

  bool DoWorkImpl(base::TimeTicks* next_run_time);

  bool InTopLevelDoWork() const;

  void InitializeThreadTaskRunnerHandle()
      EXCLUSIVE_LOCKS_REQUIRED(task_runner_lock_);

  struct MainThreadOnly {
    MainThreadOnly();
    ~MainThreadOnly();

    SequencedTaskSource* task_source = nullptr;            // Not owned.
    RunLoop::NestingObserver* nesting_observer = nullptr;  // Not owned.
    std::unique_ptr<ThreadTaskRunnerHandle> thread_task_runner_handle;

    // Indicates that we should yield DoWork ASAP.
    bool quit_pending = false;

    // Whether high resolution timing is enabled or not.
    bool in_high_res_mode = false;

    // Used to prevent redundant calls to ScheduleWork / ScheduleDelayedWork.
    bool immediate_do_work_posted = false;

    // Number of tasks processed in a single DoWork invocation.
    int work_batch_size = 1;

    // Number of DoWorks on the stack. Must be >= |nesting_depth|.
    int do_work_running_count = 0;

    // Number of nested RunLoops on the stack.
    int nesting_depth = 0;

    // Should always be < |nesting_depth|.
    int runloop_count = 0;

    // When the next scheduled delayed work should run, if any.
    TimeTicks next_delayed_do_work = TimeTicks::Max();

    bool task_execution_allowed = true;
  };

  MainThreadOnly& main_thread_only() {
    DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
    return main_thread_only_;
  }

  const MainThreadOnly& main_thread_only() const {
    DCHECK_CALLED_ON_VALID_THREAD(associated_thread_->thread_checker);
    return main_thread_only_;
  }

  // TODO(altimin): Merge with the one in SequenceManager.
  scoped_refptr<AssociatedThreadId> associated_thread_;
  MainThreadOnly main_thread_only_;

  mutable Lock task_runner_lock_;
  scoped_refptr<SingleThreadTaskRunner> task_runner_
      GUARDED_BY(task_runner_lock_);

  // OperationsController will only be started after |pump_| is set.
  base::internal::OperationsController operations_controller_;

  // Can only be set once (just before calling
  // operations_controller_.StartAcceptingOperations()). After that only read
  // access is allowed.
  std::unique_ptr<MessagePump> pump_;

  debug::TaskAnnotator task_annotator_;
  const TickClock* time_source_;  // Not owned.

  // Required to register the current thread as a sequence.
  base::internal::SequenceLocalStorageMap sequence_local_storage_map_;
  std::unique_ptr<
      base::internal::ScopedSetSequenceLocalStorageMapForCurrentThread>
      scoped_set_sequence_local_storage_map_for_current_thread_;

  DISALLOW_COPY_AND_ASSIGN(ThreadControllerWithMessagePumpImpl);
};

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base

#endif  // BASE_TASK_SEQUENCE_MANAGER_THREAD_CONTROLLER_WITH_MESSAGE_PUMP_IMPL_H_
