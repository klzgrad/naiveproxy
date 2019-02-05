// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/thread_controller_impl.h"

#include <algorithm>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_pump.h"
#include "base/run_loop.h"
#include "base/task/sequence_manager/lazy_now.h"
#include "base/task/sequence_manager/sequenced_task_source.h"
#include "base/trace_event/trace_event.h"

namespace base {
namespace sequence_manager {
namespace internal {

ThreadControllerImpl::ThreadControllerImpl(
    MessageLoopBase* message_loop_base,
    scoped_refptr<SingleThreadTaskRunner> task_runner,
    const TickClock* time_source)
    : message_loop_base_(message_loop_base),
      task_runner_(task_runner),
      associated_thread_(AssociatedThreadId::CreateUnbound()),
      message_loop_task_runner_(
          message_loop_base ? message_loop_base->GetTaskRunner() : nullptr),
      time_source_(time_source),
      weak_factory_(this) {
  immediate_do_work_closure_ =
      BindRepeating(&ThreadControllerImpl::DoWork, weak_factory_.GetWeakPtr(),
                    WorkType::kImmediate);
  delayed_do_work_closure_ =
      BindRepeating(&ThreadControllerImpl::DoWork, weak_factory_.GetWeakPtr(),
                    WorkType::kDelayed);
}

ThreadControllerImpl::~ThreadControllerImpl() = default;

ThreadControllerImpl::AnySequence::AnySequence() = default;

ThreadControllerImpl::AnySequence::~AnySequence() = default;

ThreadControllerImpl::MainSequenceOnly::MainSequenceOnly() = default;

ThreadControllerImpl::MainSequenceOnly::~MainSequenceOnly() = default;

std::unique_ptr<ThreadControllerImpl> ThreadControllerImpl::Create(
    MessageLoopBase* message_loop_base,
    const TickClock* time_source) {
  return WrapUnique(new ThreadControllerImpl(
      message_loop_base,
      message_loop_base ? message_loop_base->GetTaskRunner() : nullptr,
      time_source));
}

void ThreadControllerImpl::SetSequencedTaskSource(
    SequencedTaskSource* sequence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(associated_thread_->sequence_checker);
  DCHECK(sequence);
  DCHECK(!sequence_);
  sequence_ = sequence;
}

void ThreadControllerImpl::SetTimerSlack(TimerSlack timer_slack) {
  if (!message_loop_base_)
    return;
  message_loop_base_->SetTimerSlack(timer_slack);
}

void ThreadControllerImpl::ScheduleWork() {
  DCHECK(sequence_);
  AutoLock lock(any_sequence_lock_);
  // Don't post a DoWork if there's an immediate DoWork in flight or if we're
  // inside a top level DoWork. We can rely on a continuation being posted as
  // needed.
  if (any_sequence().immediate_do_work_posted ||
      (any_sequence().do_work_running_count > any_sequence().nesting_depth)) {
    return;
  }
  any_sequence().immediate_do_work_posted = true;

  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("sequence_manager"),
               "ThreadControllerImpl::ScheduleWork::PostTask");
  task_runner_->PostTask(FROM_HERE, immediate_do_work_closure_);
}

void ThreadControllerImpl::SetNextDelayedDoWork(LazyNow* lazy_now,
                                                TimeTicks run_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(associated_thread_->sequence_checker);
  DCHECK(sequence_);

  if (main_sequence_only().next_delayed_do_work == run_time)
    return;

  // Cancel DoWork if it was scheduled and we set an "infinite" delay now.
  if (run_time == TimeTicks::Max()) {
    cancelable_delayed_do_work_closure_.Cancel();
    main_sequence_only().next_delayed_do_work = TimeTicks::Max();
    return;
  }

  // If DoWork is running then we don't need to do anything because it will post
  // a continuation as needed. Bailing out here is by far the most common case.
  if (main_sequence_only().do_work_running_count >
      main_sequence_only().nesting_depth) {
    return;
  }

  // If DoWork is about to run then we also don't need to do anything.
  {
    AutoLock lock(any_sequence_lock_);
    if (any_sequence().immediate_do_work_posted)
      return;
  }

  base::TimeDelta delay = std::max(TimeDelta(), run_time - lazy_now->Now());
  TRACE_EVENT1(TRACE_DISABLED_BY_DEFAULT("sequence_manager"),
               "ThreadControllerImpl::SetNextDelayedDoWork::PostDelayedTask",
               "delay_ms", delay.InMillisecondsF());

  main_sequence_only().next_delayed_do_work = run_time;
  // Reset also causes cancellation of the previous DoWork task.
  cancelable_delayed_do_work_closure_.Reset(delayed_do_work_closure_);
  task_runner_->PostDelayedTask(
      FROM_HERE, cancelable_delayed_do_work_closure_.callback(), delay);
}

bool ThreadControllerImpl::RunsTasksInCurrentSequence() {
  return task_runner_->RunsTasksInCurrentSequence();
}

const TickClock* ThreadControllerImpl::GetClock() {
  return time_source_;
}

void ThreadControllerImpl::SetDefaultTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
#if DCHECK_IS_ON()
  default_task_runner_set_ = true;
#endif
  if (!message_loop_base_)
    return;
  message_loop_base_->SetTaskRunner(task_runner);
}

scoped_refptr<SingleThreadTaskRunner>
ThreadControllerImpl::GetDefaultTaskRunner() {
  return message_loop_base_->GetTaskRunner();
}

void ThreadControllerImpl::RestoreDefaultTaskRunner() {
  if (!message_loop_base_)
    return;
  message_loop_base_->SetTaskRunner(message_loop_task_runner_);
}

void ThreadControllerImpl::BindToCurrentThread(
    MessageLoopBase* message_loop_base) {
  DCHECK(!message_loop_base_);
  DCHECK(message_loop_base);
#if DCHECK_IS_ON()
  DCHECK(!default_task_runner_set_) << "This would undo SetDefaultTaskRunner";
#endif
  message_loop_base_ = message_loop_base;
  task_runner_ = message_loop_base->GetTaskRunner();
  message_loop_task_runner_ = message_loop_base->GetTaskRunner();
}

void ThreadControllerImpl::BindToCurrentThread(
    std::unique_ptr<MessagePump> message_pump) {
  NOTREACHED();
}

void ThreadControllerImpl::WillQueueTask(PendingTask* pending_task) {
  task_annotator_.WillQueueTask("SequenceManager::PostTask", pending_task);
}

void ThreadControllerImpl::DoWork(WorkType work_type) {
  TRACE_EVENT0("sequence_manager", "ThreadControllerImpl::DoWork");

  DCHECK_CALLED_ON_VALID_SEQUENCE(associated_thread_->sequence_checker);
  DCHECK(sequence_);

  {
    AutoLock lock(any_sequence_lock_);
    if (work_type == WorkType::kImmediate)
      any_sequence().immediate_do_work_posted = false;
    any_sequence().do_work_running_count++;
  }

  main_sequence_only().do_work_running_count++;

  WeakPtr<ThreadControllerImpl> weak_ptr = weak_factory_.GetWeakPtr();
  // TODO(scheduler-dev): Consider moving to a time based work batch instead.
  for (int i = 0; i < main_sequence_only().work_batch_size_; i++) {
    Optional<PendingTask> task = sequence_->TakeTask();
    if (!task)
      break;

    {
      TRACE_TASK_EXECUTION("ThreadControllerImpl::RunTask", *task);
      // Trace-parsing tools (Lighthouse, etc) consume this event to determine
      // long tasks. See https://crbug.com/874982
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("lighthouse"), "RunTask");
      task_annotator_.RunTask("ThreadControllerImpl::RunTask", &*task);
    }

    if (!weak_ptr)
      return;

    sequence_->DidRunTask();

    // NOTE: https://crbug.com/828835.
    // When we're running inside a nested RunLoop it may quit anytime, so any
    // outstanding pending tasks must run in the outer RunLoop
    // (see SequenceManagerTestWithMessageLoop.QuitWhileNested test).
    // Unfortunately, it's MessageLoop who's receving that signal and we can't
    // know it before we return from DoWork, hence, OnExitNestedRunLoop
    // will be called later. Since we must implement ThreadController and
    // SequenceManager in conformance with MessageLoop task runners, we need
    // to disable this batching optimization while nested.
    // Implementing MessagePump::Delegate ourselves will help to resolve this
    // issue.
    if (main_sequence_only().nesting_depth > 0)
      break;
  }

  main_sequence_only().do_work_running_count--;

  {
    AutoLock lock(any_sequence_lock_);
    any_sequence().do_work_running_count--;
    DCHECK_GE(any_sequence().do_work_running_count, 0);
    LazyNow lazy_now(time_source_);
    TimeDelta delay_till_next_task = sequence_->DelayTillNextTask(&lazy_now);
    // The OnSystemIdle callback allows the TimeDomains to advance virtual time
    // in which case we now have immediate word to do.
    if (delay_till_next_task <= TimeDelta() || sequence_->OnSystemIdle()) {
      // The next task needs to run immediately, post a continuation if needed.
      if (!any_sequence().immediate_do_work_posted) {
        any_sequence().immediate_do_work_posted = true;
        task_runner_->PostTask(FROM_HERE, immediate_do_work_closure_);
      }
      return;
    }
    if (delay_till_next_task < TimeDelta::Max()) {
      // The next task needs to run after a delay, post a continuation if
      // needed.
      TimeTicks next_task_at = lazy_now.Now() + delay_till_next_task;
      if (next_task_at != main_sequence_only().next_delayed_do_work) {
        main_sequence_only().next_delayed_do_work = next_task_at;
        cancelable_delayed_do_work_closure_.Reset(delayed_do_work_closure_);
        task_runner_->PostDelayedTask(
            FROM_HERE, cancelable_delayed_do_work_closure_.callback(),
            delay_till_next_task);
      }
      return;
    }
    // There is no next task scheduled.
    main_sequence_only().next_delayed_do_work = TimeTicks::Max();
  }
}

void ThreadControllerImpl::AddNestingObserver(
    RunLoop::NestingObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(associated_thread_->sequence_checker);
  nesting_observer_ = observer;
  RunLoop::AddNestingObserverOnCurrentThread(this);
}

void ThreadControllerImpl::RemoveNestingObserver(
    RunLoop::NestingObserver* observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(associated_thread_->sequence_checker);
  DCHECK_EQ(observer, nesting_observer_);
  nesting_observer_ = nullptr;
  RunLoop::RemoveNestingObserverOnCurrentThread(this);
}

const scoped_refptr<AssociatedThreadId>&
ThreadControllerImpl::GetAssociatedThread() const {
  return associated_thread_;
}

void ThreadControllerImpl::OnBeginNestedRunLoop() {
  main_sequence_only().nesting_depth++;
  {
    // We just entered a nested run loop, make sure there's a DoWork posted or
    // the system will grind to a halt.
    AutoLock lock(any_sequence_lock_);
    any_sequence().nesting_depth++;
    if (!any_sequence().immediate_do_work_posted) {
      any_sequence().immediate_do_work_posted = true;
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("sequence_manager"),
                   "ThreadControllerImpl::OnBeginNestedRunLoop::PostTask");
      task_runner_->PostTask(FROM_HERE, immediate_do_work_closure_);
    }
  }
  if (nesting_observer_)
    nesting_observer_->OnBeginNestedRunLoop();
}

void ThreadControllerImpl::OnExitNestedRunLoop() {
  main_sequence_only().nesting_depth--;
  {
    AutoLock lock(any_sequence_lock_);
    any_sequence().nesting_depth--;
    DCHECK_GE(any_sequence().nesting_depth, 0);
  }
  if (nesting_observer_)
    nesting_observer_->OnExitNestedRunLoop();
}

void ThreadControllerImpl::SetWorkBatchSize(int work_batch_size) {
  main_sequence_only().work_batch_size_ = work_batch_size;
}

void ThreadControllerImpl::SetTaskExecutionAllowed(bool allowed) {
  NOTREACHED();
}

bool ThreadControllerImpl::IsTaskExecutionAllowed() const {
  return true;
}

bool ThreadControllerImpl::ShouldQuitRunLoopWhenIdle() {
  // The MessageLoop does not expose the API needed to support this query.
  return false;
}

MessagePump* ThreadControllerImpl::GetBoundMessagePump() const {
  return nullptr;
}

#if defined(OS_IOS) || defined(OS_ANDROID)
void ThreadControllerImpl::AttachToMessagePump() {
  NOTREACHED();
}
#endif

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base
