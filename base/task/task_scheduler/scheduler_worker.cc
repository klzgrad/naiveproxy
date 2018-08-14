// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task/task_scheduler/scheduler_worker.h"

#include <stddef.h>

#include <utility>

#include "base/compiler_specific.h"
#include "base/debug/alias.h"
#include "base/logging.h"
#include "base/task/task_scheduler/environment_config.h"
#include "base/task/task_scheduler/scheduler_worker_observer.h"
#include "base/task/task_scheduler/task_tracker.h"
#include "base/trace_event/trace_event.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#elif defined(OS_WIN)
#include "base/win/com_init_check_hook.h"
#include "base/win/scoped_com_initializer.h"
#endif

namespace base {
namespace internal {

void SchedulerWorker::Delegate::WaitForWork(WaitableEvent* wake_up_event) {
  DCHECK(wake_up_event);
  const TimeDelta sleep_time = GetSleepTimeout();
  if (sleep_time.is_max()) {
    // Calling TimedWait with TimeDelta::Max is not recommended per
    // http://crbug.com/465948.
    wake_up_event->Wait();
  } else {
    wake_up_event->TimedWait(sleep_time);
  }
}

SchedulerWorker::SchedulerWorker(
    ThreadPriority priority_hint,
    std::unique_ptr<Delegate> delegate,
    TrackedRef<TaskTracker> task_tracker,
    const SchedulerLock* predecessor_lock,
    SchedulerBackwardCompatibility backward_compatibility)
    : thread_lock_(predecessor_lock),
      delegate_(std::move(delegate)),
      task_tracker_(std::move(task_tracker)),
      priority_hint_(priority_hint),
      current_thread_priority_(GetDesiredThreadPriority())
#if defined(OS_WIN) && !defined(COM_INIT_CHECK_HOOK_ENABLED)
      ,
      backward_compatibility_(backward_compatibility)
#endif
{
  DCHECK(delegate_);
  DCHECK(task_tracker_);
  DCHECK(CanUseBackgroundPriorityForSchedulerWorker() ||
         priority_hint_ != ThreadPriority::BACKGROUND);
}

bool SchedulerWorker::Start(
    SchedulerWorkerObserver* scheduler_worker_observer) {
  AutoSchedulerLock auto_lock(thread_lock_);
  DCHECK(thread_handle_.is_null());

  if (should_exit_.IsSet())
    return true;

  DCHECK(!scheduler_worker_observer_);
  scheduler_worker_observer_ = scheduler_worker_observer;

  self_ = this;

  constexpr size_t kDefaultStackSize = 0;
  PlatformThread::CreateWithPriority(kDefaultStackSize, this, &thread_handle_,
                                     current_thread_priority_);

  if (thread_handle_.is_null()) {
    self_ = nullptr;
    return false;
  }

  return true;
}

void SchedulerWorker::WakeUp() {
  // Calling WakeUp() after Cleanup() or Join() is wrong because the
  // SchedulerWorker cannot run more tasks.
  DCHECK(!join_called_for_testing_.IsSet());
  DCHECK(!should_exit_.IsSet());
  wake_up_event_.Signal();
}

void SchedulerWorker::JoinForTesting() {
  DCHECK(!join_called_for_testing_.IsSet());
  join_called_for_testing_.Set();
  wake_up_event_.Signal();

  PlatformThreadHandle thread_handle;

  {
    AutoSchedulerLock auto_lock(thread_lock_);
    DCHECK(!thread_handle_.is_null());
    thread_handle = thread_handle_;
    // Reset |thread_handle_| so it isn't joined by the destructor.
    thread_handle_ = PlatformThreadHandle();
  }

  PlatformThread::Join(thread_handle);
}

bool SchedulerWorker::ThreadAliveForTesting() const {
  AutoSchedulerLock auto_lock(thread_lock_);
  return !thread_handle_.is_null();
}

SchedulerWorker::~SchedulerWorker() {
  AutoSchedulerLock auto_lock(thread_lock_);

  // If |thread_handle_| wasn't joined, detach it.
  if (!thread_handle_.is_null()) {
    DCHECK(!join_called_for_testing_.IsSet());
    PlatformThread::Detach(thread_handle_);
  }
}

void SchedulerWorker::Cleanup() {
  DCHECK(!should_exit_.IsSet());
  should_exit_.Set();
  wake_up_event_.Signal();
}

void SchedulerWorker::BeginUnusedPeriod() {
  AutoSchedulerLock auto_lock(thread_lock_);
  DCHECK(last_used_time_.is_null());
  last_used_time_ = TimeTicks::Now();
}

void SchedulerWorker::EndUnusedPeriod() {
  AutoSchedulerLock auto_lock(thread_lock_);
  DCHECK(!last_used_time_.is_null());
  last_used_time_ = TimeTicks();
}

TimeTicks SchedulerWorker::GetLastUsedTime() const {
  AutoSchedulerLock auto_lock(thread_lock_);
  return last_used_time_;
}

bool SchedulerWorker::ShouldExit() const {
  // The ordering of the checks is important below. This SchedulerWorker may be
  // released and outlive |task_tracker_| in unit tests. However, when the
  // SchedulerWorker is released, |should_exit_| will be set, so check that
  // first.
  return should_exit_.IsSet() || join_called_for_testing_.IsSet() ||
         task_tracker_->IsShutdownComplete();
}

ThreadPriority SchedulerWorker::GetDesiredThreadPriority() const {
  // To avoid shutdown hangs, disallow a priority below NORMAL during shutdown
  if (task_tracker_->HasShutdownStarted())
    return ThreadPriority::NORMAL;

  return priority_hint_;
}

void SchedulerWorker::UpdateThreadPriority(
    ThreadPriority desired_thread_priority) {
  if (desired_thread_priority == current_thread_priority_)
    return;

  PlatformThread::SetCurrentThreadPriority(desired_thread_priority);
  current_thread_priority_ = desired_thread_priority;
}

void SchedulerWorker::ThreadMain() {
  if (priority_hint_ == ThreadPriority::BACKGROUND) {
    switch (delegate_->GetThreadLabel()) {
      case ThreadLabel::POOLED:
        RunBackgroundPooledWorker();
        return;
      case ThreadLabel::SHARED:
        RunBackgroundSharedWorker();
        return;
      case ThreadLabel::DEDICATED:
        RunBackgroundDedicatedWorker();
        return;
#if defined(OS_WIN)
      case ThreadLabel::SHARED_COM:
        RunBackgroundSharedCOMWorker();
        return;
      case ThreadLabel::DEDICATED_COM:
        RunBackgroundDedicatedCOMWorker();
        return;
#endif  // defined(OS_WIN)
    }
  }

  switch (delegate_->GetThreadLabel()) {
    case ThreadLabel::POOLED:
      RunPooledWorker();
      return;
    case ThreadLabel::SHARED:
      RunSharedWorker();
      return;
    case ThreadLabel::DEDICATED:
      RunDedicatedWorker();
      return;
#if defined(OS_WIN)
    case ThreadLabel::SHARED_COM:
      RunSharedCOMWorker();
      return;
    case ThreadLabel::DEDICATED_COM:
      RunDedicatedCOMWorker();
      return;
#endif  // defined(OS_WIN)
  }
}

NOINLINE void SchedulerWorker::RunPooledWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunBackgroundPooledWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunSharedWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunBackgroundSharedWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunDedicatedWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunBackgroundDedicatedWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

#if defined(OS_WIN)
NOINLINE void SchedulerWorker::RunSharedCOMWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunBackgroundSharedCOMWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunDedicatedCOMWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}

NOINLINE void SchedulerWorker::RunBackgroundDedicatedCOMWorker() {
  const int line_number = __LINE__;
  RunWorker();
  base::debug::Alias(&line_number);
}
#endif  // defined(OS_WIN)

void SchedulerWorker::RunWorker() {
  DCHECK_EQ(self_, this);
  TRACE_EVENT_BEGIN0("task_scheduler", "SchedulerWorkerThread active");

  if (scheduler_worker_observer_)
    scheduler_worker_observer_->OnSchedulerWorkerMainEntry();

  delegate_->OnMainEntry(this);

  // A SchedulerWorker starts out waiting for work.
  {
    TRACE_EVENT_END0("task_scheduler", "SchedulerWorkerThread active");
    delegate_->WaitForWork(&wake_up_event_);
    TRACE_EVENT_BEGIN0("task_scheduler", "SchedulerWorkerThread active");
  }

// When defined(COM_INIT_CHECK_HOOK_ENABLED), ignore
// SchedulerBackwardCompatibility::INIT_COM_STA to find incorrect uses of
// COM that should be running in a COM STA Task Runner.
#if defined(OS_WIN) && !defined(COM_INIT_CHECK_HOOK_ENABLED)
  std::unique_ptr<win::ScopedCOMInitializer> com_initializer;
  if (backward_compatibility_ == SchedulerBackwardCompatibility::INIT_COM_STA)
    com_initializer = std::make_unique<win::ScopedCOMInitializer>();
#endif

  while (!ShouldExit()) {
#if defined(OS_MACOSX)
    mac::ScopedNSAutoreleasePool autorelease_pool;
#endif

    UpdateThreadPriority(GetDesiredThreadPriority());

    // Get the sequence containing the next task to execute.
    scoped_refptr<Sequence> sequence = delegate_->GetWork(this);
    if (!sequence) {
      // Exit immediately if GetWork() resulted in detaching this worker.
      if (ShouldExit())
        break;

      TRACE_EVENT_END0("task_scheduler", "SchedulerWorkerThread active");
      delegate_->WaitForWork(&wake_up_event_);
      TRACE_EVENT_BEGIN0("task_scheduler", "SchedulerWorkerThread active");
      continue;
    }

    sequence =
        task_tracker_->RunAndPopNextTask(std::move(sequence), delegate_.get());

    delegate_->DidRunTask();

    // Re-enqueue |sequence| if allowed by RunNextTask().
    if (sequence)
      delegate_->ReEnqueueSequence(std::move(sequence));

    // Calling WakeUp() guarantees that this SchedulerWorker will run Tasks from
    // Sequences returned by the GetWork() method of |delegate_| until it
    // returns nullptr. Resetting |wake_up_event_| here doesn't break this
    // invariant and avoids a useless loop iteration before going to sleep if
    // WakeUp() is called while this SchedulerWorker is awake.
    wake_up_event_.Reset();
  }

  // Important: It is unsafe to access unowned state (e.g. |task_tracker_|)
  // after invoking OnMainExit().

  delegate_->OnMainExit(this);

  if (scheduler_worker_observer_)
    scheduler_worker_observer_->OnSchedulerWorkerMainExit();

  // Release the self-reference to |this|. This can result in deleting |this|
  // and as such no more member accesses should be made after this point.
  self_ = nullptr;

  TRACE_EVENT_END0("task_scheduler", "SchedulerWorkerThread active");
}

}  // namespace internal
}  // namespace base
