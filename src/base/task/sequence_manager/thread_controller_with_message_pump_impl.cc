// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/sequence_manager/thread_controller_with_message_pump_impl.h"

#include "base/auto_reset.h"
#include "base/message_loop/message_pump.h"
#include "base/time/tick_clock.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"

#if defined(OS_IOS)
#include "base/message_loop/message_pump_mac.h"
#elif defined(OS_ANDROID)
#include "base/message_loop/message_pump_android.h"
#endif

namespace base {
namespace sequence_manager {
namespace internal {
namespace {

// Returns |next_run_time| capped at 1 day from |lazy_now|. This is used to
// mitigate https://crbug.com/850450 where some platforms are unhappy with
// delays > 100,000,000 seconds. In practice, a diagnosis metric showed that no
// sleep > 1 hour ever completes (always interrupted by an earlier MessageLoop
// event) and 99% of completed sleeps are the ones scheduled for <= 1 second.
// Details @ https://crrev.com/c/1142589.
TimeTicks CapAtOneDay(TimeTicks next_run_time, LazyNow* lazy_now) {
  return std::min(next_run_time, lazy_now->Now() + TimeDelta::FromDays(1));
}

}  // namespace

ThreadControllerWithMessagePumpImpl::ThreadControllerWithMessagePumpImpl(
    const TickClock* time_source)
    : associated_thread_(AssociatedThreadId::CreateUnbound()),
      time_source_(time_source) {}

ThreadControllerWithMessagePumpImpl::ThreadControllerWithMessagePumpImpl(
    std::unique_ptr<MessagePump> message_pump,
    const TickClock* time_source)
    : ThreadControllerWithMessagePumpImpl(time_source) {
  BindToCurrentThread(std::move(message_pump));
}

ThreadControllerWithMessagePumpImpl::~ThreadControllerWithMessagePumpImpl() {
  operations_controller_.ShutdownAndWaitForZeroOperations();
  // Destructors of MessagePump::Delegate and ThreadTaskRunnerHandle
  // will do all the clean-up.
  // ScopedSetSequenceLocalStorageMapForCurrentThread destructor will
  // de-register the current thread as a sequence.
}

// static
std::unique_ptr<ThreadControllerWithMessagePumpImpl>
ThreadControllerWithMessagePumpImpl::CreateUnbound(
    const TickClock* time_source) {
  return base::WrapUnique(new ThreadControllerWithMessagePumpImpl(time_source));
}

ThreadControllerWithMessagePumpImpl::MainThreadOnly::MainThreadOnly() = default;

ThreadControllerWithMessagePumpImpl::MainThreadOnly::~MainThreadOnly() =
    default;

void ThreadControllerWithMessagePumpImpl::SetSequencedTaskSource(
    SequencedTaskSource* task_source) {
  DCHECK(task_source);
  DCHECK(!main_thread_only().task_source);
  main_thread_only().task_source = task_source;
}

void ThreadControllerWithMessagePumpImpl::BindToCurrentThread(
    MessageLoopBase* message_loop_base) {
  NOTREACHED()
      << "ThreadControllerWithMessagePumpImpl doesn't support MessageLoops";
}

void ThreadControllerWithMessagePumpImpl::BindToCurrentThread(
    std::unique_ptr<MessagePump> message_pump) {
  associated_thread_->BindToCurrentThread();
  pump_ = std::move(message_pump);
  RunLoop::RegisterDelegateForCurrentThread(this);
  scoped_set_sequence_local_storage_map_for_current_thread_ = std::make_unique<
      base::internal::ScopedSetSequenceLocalStorageMapForCurrentThread>(
      &sequence_local_storage_map_);
  {
    AutoLock task_runner_lock(task_runner_lock_);
    if (task_runner_)
      InitializeThreadTaskRunnerHandle();
  }
  if (operations_controller_.StartAcceptingOperations())
    ScheduleWork();
}

void ThreadControllerWithMessagePumpImpl::SetWorkBatchSize(
    int work_batch_size) {
  DCHECK_GE(work_batch_size, 1);
  main_thread_only().work_batch_size = work_batch_size;
}

void ThreadControllerWithMessagePumpImpl::SetTimerSlack(
    TimerSlack timer_slack) {
  DCHECK(RunsTasksInCurrentSequence());
  pump_->SetTimerSlack(timer_slack);
}

void ThreadControllerWithMessagePumpImpl::WillQueueTask(
    PendingTask* pending_task) {
  task_annotator_.WillQueueTask("ThreadController::Task", pending_task);
}

void ThreadControllerWithMessagePumpImpl::ScheduleWork() {
  auto operation_token = operations_controller_.TryBeginOperation();
  if (!operation_token)
    return;

  // This assumes that cross thread ScheduleWork isn't frequent enough to
  // warrant ScheduleWork deduplication.
  if (RunsTasksInCurrentSequence()) {
    // Don't post a DoWork if there's an immediate DoWork in flight or if we're
    // inside a top level DoWork. We can rely on a continuation being posted as
    // needed. We need to avoid this inside DoDelayedWork, however, because
    // returning true there doesn't guarantee work to get scheduled.
    // TODO(skyostil@): Simplify this once DoWork/DoDelayedWork get merged.
    if (main_thread_only().immediate_do_work_posted ||
        (InTopLevelDoWork() && !main_thread_only().doing_delayed_work)) {
      return;
    }
    main_thread_only().immediate_do_work_posted = true;
  }
  pump_->ScheduleWork();
}

void ThreadControllerWithMessagePumpImpl::SetNextDelayedDoWork(
    LazyNow* lazy_now,
    TimeTicks run_time) {
  DCHECK_LT(lazy_now->Now(), run_time);

  if (main_thread_only().next_delayed_do_work == run_time)
    return;

  run_time = CapAtOneDay(run_time, lazy_now);
  main_thread_only().next_delayed_do_work = run_time;

  // Do not call ScheduleDelayedWork if there is an immediate DoWork scheduled.
  // We can rely on calling ScheduleDelayedWork from the next DoWork call.
  if (main_thread_only().immediate_do_work_posted || InTopLevelDoWork())
    return;

  // |pump_| can't be null as all postTasks are cross-thread before binding,
  // and delayed cross-thread postTasks do the thread hop through an immediate
  // task.
  pump_->ScheduleDelayedWork(run_time);
}

const TickClock* ThreadControllerWithMessagePumpImpl::GetClock() {
  return time_source_;
}

bool ThreadControllerWithMessagePumpImpl::RunsTasksInCurrentSequence() {
  return associated_thread_->IsBoundToCurrentThread();
}

void ThreadControllerWithMessagePumpImpl::SetDefaultTaskRunner(
    scoped_refptr<SingleThreadTaskRunner> task_runner) {
  AutoLock lock(task_runner_lock_);
  task_runner_ = task_runner;
  if (associated_thread_->IsBound()) {
    DCHECK(associated_thread_->IsBoundToCurrentThread());
    // Thread task runner handle will be created in BindToCurrentThread().
    InitializeThreadTaskRunnerHandle();
  }
}

void ThreadControllerWithMessagePumpImpl::InitializeThreadTaskRunnerHandle() {
  // Only one ThreadTaskRunnerHandle can exist at any time,
  // so reset the old one.
  main_thread_only().thread_task_runner_handle.reset();
  main_thread_only().thread_task_runner_handle =
      std::make_unique<ThreadTaskRunnerHandle>(task_runner_);
}

scoped_refptr<SingleThreadTaskRunner>
ThreadControllerWithMessagePumpImpl::GetDefaultTaskRunner() {
  AutoLock lock(task_runner_lock_);
  return task_runner_;
}

void ThreadControllerWithMessagePumpImpl::RestoreDefaultTaskRunner() {
  // There's no default task runner unlike with the MessageLoop.
  main_thread_only().thread_task_runner_handle.reset();
}

void ThreadControllerWithMessagePumpImpl::AddNestingObserver(
    RunLoop::NestingObserver* observer) {
  DCHECK(!main_thread_only().nesting_observer);
  DCHECK(observer);
  main_thread_only().nesting_observer = observer;
  RunLoop::AddNestingObserverOnCurrentThread(this);
}

void ThreadControllerWithMessagePumpImpl::RemoveNestingObserver(
    RunLoop::NestingObserver* observer) {
  DCHECK_EQ(main_thread_only().nesting_observer, observer);
  main_thread_only().nesting_observer = nullptr;
  RunLoop::RemoveNestingObserverOnCurrentThread(this);
}

const scoped_refptr<AssociatedThreadId>&
ThreadControllerWithMessagePumpImpl::GetAssociatedThread() const {
  return associated_thread_;
}

bool ThreadControllerWithMessagePumpImpl::DoWork() {
  main_thread_only().immediate_do_work_posted = false;
  bool ran_task = false;
  LazyNow continuation_lazy_now(time_source_);
  TimeDelta delay_till_next_task =
      DoWorkImpl(&continuation_lazy_now, &ran_task);
  // Schedule a continuation.
  // TODO(altimin, gab): Make this more efficient by merging DoWork
  // and DoDelayedWork and allowing returning base::TimeTicks() when we have
  // immediate work.
  if (delay_till_next_task.is_zero()) {
    // Need to run new work immediately, but due to the contract of DoWork we
    // only need to return true to ensure that happens.
    main_thread_only().immediate_do_work_posted = true;
    return true;
  }
  // DoDelayedWork always follows DoWork, (although the inverse is not true) so
  // we don't need to schedule a delayed wakeup here.
  main_thread_only().immediate_do_work_posted = ran_task;
  return ran_task;
}

bool ThreadControllerWithMessagePumpImpl::DoDelayedWork(
    TimeTicks* next_run_time) {
  AutoReset<bool> delayed_scope(&main_thread_only().doing_delayed_work, true);
  LazyNow continuation_lazy_now(time_source_);
  bool ran_task = false;
  TimeDelta delay_till_next_task =
      DoWorkImpl(&continuation_lazy_now, &ran_task);
  // Schedule a continuation.
  // TODO(altimin, gab): Make this more efficient by merging DoWork
  // and DoDelayedWork and allowing returning base::TimeTicks() when we have
  // immediate work.
  if (delay_till_next_task.is_zero()) {
    *next_run_time = TimeTicks();
    // Make sure a DoWork is scheduled.
    if (!main_thread_only().immediate_do_work_posted) {
      main_thread_only().immediate_do_work_posted = true;
      pump_->ScheduleWork();
    }
  } else if (delay_till_next_task != TimeDelta::Max()) {
    // The MessagePump will call ScheduleDelayedWork on our behalf, so we need
    // to update |main_thread_only().next_delayed_do_work|.
    main_thread_only().next_delayed_do_work =
        continuation_lazy_now.Now() + delay_till_next_task;

    // Cancels any previously scheduled delayed wake-ups.
    *next_run_time = CapAtOneDay(main_thread_only().next_delayed_do_work,
                                 &continuation_lazy_now);
  } else {
    *next_run_time = base::TimeTicks();
  }
  return ran_task;
}

TimeDelta ThreadControllerWithMessagePumpImpl::DoWorkImpl(
    LazyNow* continuation_lazy_now,
    bool* ran_task) {
  if (!main_thread_only().task_execution_allowed)
    return TimeDelta::Max();

  // Keep this in-sync with
  // third_party/catapult/tracing/tracing/extras/chrome/event_finder_utils.html
  // TODO(alexclarke): Rename this event to whatever we end up calling this
  // after the DoWork / DoDelayed work merge.
  TRACE_EVENT0("sequence_manager", "ThreadControllerImpl::RunTask");

  DCHECK(main_thread_only().task_source);
  main_thread_only().do_work_running_count++;

  for (int i = 0; i < main_thread_only().work_batch_size; i++) {
    Optional<PendingTask> task = main_thread_only().task_source->TakeTask();
    if (!task)
      break;

    // Execute the task and assume the worst: it is probably not reentrant.
    main_thread_only().task_execution_allowed = false;

    TRACE_TASK_EXECUTION("ThreadController::Task", *task);
    // Trace-parsing tools (Lighthouse, etc) consume this event to determine
    // long tasks. See https://crbug.com/874982
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("lighthouse"), "RunTask");
    task_annotator_.RunTask("ThreadController::Task", &*task);
    *ran_task = true;

    main_thread_only().task_execution_allowed = true;
    main_thread_only().task_source->DidRunTask();

    // When Quit() is called we must stop running the batch because the caller
    // expects per-task granularity.
    if (main_thread_only().quit_pending)
      break;
  }

  main_thread_only().do_work_running_count--;

  if (main_thread_only().quit_pending)
    return TimeDelta::Max();

  TimeDelta do_work_delay =
      main_thread_only().task_source->DelayTillNextTask(continuation_lazy_now);
  DCHECK_GE(do_work_delay, TimeDelta());
  return do_work_delay;
}

bool ThreadControllerWithMessagePumpImpl::InTopLevelDoWork() const {
  return main_thread_only().do_work_running_count >
         main_thread_only().nesting_depth;
}

bool ThreadControllerWithMessagePumpImpl::DoIdleWork() {
#if defined(OS_WIN)
  bool need_high_res_mode =
      main_thread_only().task_source->HasPendingHighResolutionTasks();
  if (main_thread_only().in_high_res_mode != need_high_res_mode) {
    // On Windows we activate the high resolution timer so that the wait
    // _if_ triggered by the timer happens with good resolution. If we don't
    // do this the default resolution is 15ms which might not be acceptable
    // for some tasks.
    main_thread_only().in_high_res_mode = need_high_res_mode;
    Time::ActivateHighResolutionTimer(need_high_res_mode);
  }
#endif  // defined(OS_WIN)

  if (main_thread_only().task_source->OnSystemIdle()) {
    // The OnSystemIdle() callback resulted in more immediate work, so schedule
    // a DoWork callback. For some message pumps returning true from here is
    // sufficient to do that but not on mac.
    pump_->ScheduleWork();
    return false;
  }

  // RunLoop::Delegate knows whether we called Run() or RunUntilIdle().
  if (ShouldQuitWhenIdle())
    Quit();

  return false;
}

void ThreadControllerWithMessagePumpImpl::Run(bool application_tasks_allowed) {
  DCHECK(RunsTasksInCurrentSequence());
  // Quit may have been called outside of a Run(), so |quit_pending| might be
  // true here. We can't use InTopLevelDoWork() in Quit() as this call may be
  // outside top-level DoWork but still in Run().
  main_thread_only().quit_pending = false;
  main_thread_only().runloop_count++;
  if (application_tasks_allowed && !main_thread_only().task_execution_allowed) {
    // Allow nested task execution as explicitly requested.
    DCHECK(RunLoop::IsNestedOnCurrentThread());
    main_thread_only().task_execution_allowed = true;
    pump_->Run(this);
    main_thread_only().task_execution_allowed = false;
  } else {
    pump_->Run(this);
  }
  main_thread_only().runloop_count--;
  main_thread_only().quit_pending = false;
}

void ThreadControllerWithMessagePumpImpl::OnBeginNestedRunLoop() {
  main_thread_only().nesting_depth++;
  if (main_thread_only().nesting_observer)
    main_thread_only().nesting_observer->OnBeginNestedRunLoop();
}

void ThreadControllerWithMessagePumpImpl::OnExitNestedRunLoop() {
  main_thread_only().nesting_depth--;
  DCHECK_GE(main_thread_only().nesting_depth, 0);
  if (main_thread_only().nesting_observer)
    main_thread_only().nesting_observer->OnExitNestedRunLoop();
}

void ThreadControllerWithMessagePumpImpl::Quit() {
  DCHECK(RunsTasksInCurrentSequence());
  // Interrupt a batch of work.
  main_thread_only().quit_pending = true;
  // If we're in a nested RunLoop, continuation will be posted if necessary.
  pump_->Quit();
}

void ThreadControllerWithMessagePumpImpl::EnsureWorkScheduled() {
  pump_->ScheduleWork();
  main_thread_only().immediate_do_work_posted = true;
}

void ThreadControllerWithMessagePumpImpl::SetTaskExecutionAllowed(
    bool allowed) {
  if (allowed)
    EnsureWorkScheduled();
  main_thread_only().task_execution_allowed = allowed;
}

bool ThreadControllerWithMessagePumpImpl::IsTaskExecutionAllowed() const {
  return main_thread_only().task_execution_allowed;
}

MessagePump* ThreadControllerWithMessagePumpImpl::GetBoundMessagePump() const {
  return pump_.get();
}

#if defined(OS_IOS)
void ThreadControllerWithMessagePumpImpl::AttachToMessagePump() {
  static_cast<MessagePumpUIApplication*>(pump_.get())->Attach(this);
}
#elif defined(OS_ANDROID)
void ThreadControllerWithMessagePumpImpl::AttachToMessagePump() {
  static_cast<MessagePumpForUI*>(pump_.get())->Attach(this);
}
#endif

bool ThreadControllerWithMessagePumpImpl::ShouldQuitRunLoopWhenIdle() {
  if (main_thread_only().runloop_count == 0)
    return false;
  // It's only safe to call ShouldQuitWhenIdle() when in a RunLoop.
  return ShouldQuitWhenIdle();
}

}  // namespace internal
}  // namespace sequence_manager
}  // namespace base
