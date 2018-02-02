// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/task_scheduler/scheduler_worker.h"

#include <stddef.h>

#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/task_scheduler/task_tracker.h"

#if defined(OS_MACOSX)
#include "base/mac/scoped_nsautorelease_pool.h"
#elif defined(OS_WIN)
#include "base/win/com_init_check_hook.h"
#include "base/win/scoped_com_initializer.h"
#endif

namespace base {
namespace internal {

class SchedulerWorker::Thread : public PlatformThread::Delegate {
 public:
  ~Thread() override = default;

  static std::unique_ptr<Thread> Create(scoped_refptr<SchedulerWorker> outer) {
    std::unique_ptr<Thread> thread(new Thread(std::move(outer)));
    thread->Initialize();
    if (thread->thread_handle_.is_null())
      return nullptr;
    return thread;
  }

  // PlatformThread::Delegate.
  void ThreadMain() override {
    outer_->delegate_->OnMainEntry(outer_.get());

    // A SchedulerWorker starts out waiting for work.
    outer_->delegate_->WaitForWork(&wake_up_event_);

    // When defined(COM_INIT_CHECK_HOOK_ENABLED), ignore
    // SchedulerBackwardCompatibility::INIT_COM_STA to find incorrect uses of
    // COM that should be running in a COM STA Task Runner.
#if defined(OS_WIN) && !defined(COM_INIT_CHECK_HOOK_ENABLED)
    std::unique_ptr<win::ScopedCOMInitializer> com_initializer;
    if (outer_->backward_compatibility_ ==
        SchedulerBackwardCompatibility::INIT_COM_STA) {
      com_initializer = std::make_unique<win::ScopedCOMInitializer>();
    }
#endif

    while (!outer_->ShouldExit()) {
      DCHECK(outer_);

#if defined(OS_MACOSX)
      mac::ScopedNSAutoreleasePool autorelease_pool;
#endif

      UpdateThreadPriority(GetDesiredThreadPriority());

      // Get the sequence containing the next task to execute.
      scoped_refptr<Sequence> sequence =
          outer_->delegate_->GetWork(outer_.get());
      if (!sequence) {
        outer_->delegate_->WaitForWork(&wake_up_event_);
        continue;
      }

      sequence = outer_->task_tracker_->RunNextTask(std::move(sequence),
                                                    outer_->delegate_.get());

      outer_->delegate_->DidRunTask();

      // Re-enqueue |sequence| if allowed by RunNextTask().
      if (sequence)
        outer_->delegate_->ReEnqueueSequence(std::move(sequence));

      // Calling WakeUp() guarantees that this SchedulerWorker will run
      // Tasks from Sequences returned by the GetWork() method of |delegate_|
      // until it returns nullptr. Resetting |wake_up_event_| here doesn't break
      // this invariant and avoids a useless loop iteration before going to
      // sleep if WakeUp() is called while this SchedulerWorker is awake.
      wake_up_event_.Reset();
    }

    outer_->delegate_->OnMainExit(outer_.get());

    // Break the ownership circle between SchedulerWorker and Thread.
    // This can result in deleting |this| and as such no more member accesses
    // should be made after this point.
    outer_ = nullptr;
  }

  void Join() { PlatformThread::Join(thread_handle_); }

  void Detach() { PlatformThread::Detach(thread_handle_); }

  void WakeUp() { wake_up_event_.Signal(); }

 private:
  Thread(scoped_refptr<SchedulerWorker> outer)
      : outer_(std::move(outer)),
        wake_up_event_(WaitableEvent::ResetPolicy::MANUAL,
                       WaitableEvent::InitialState::NOT_SIGNALED),
        current_thread_priority_(GetDesiredThreadPriority()) {
    DCHECK(outer_);
  }

  void Initialize() {
    constexpr size_t kDefaultStackSize = 0;
    PlatformThread::CreateWithPriority(kDefaultStackSize, this, &thread_handle_,
                                       current_thread_priority_);
  }

  // Returns the priority for which the thread should be set based on the
  // priority hint, current shutdown state, and platform capabilities.
  ThreadPriority GetDesiredThreadPriority() {
    DCHECK(outer_);

    // All threads have a NORMAL priority when Lock doesn't handle multiple
    // thread priorities.
    if (!Lock::HandlesMultipleThreadPriorities())
      return ThreadPriority::NORMAL;

    // To avoid shutdown hangs, disallow a priority below NORMAL during
    // shutdown. If thread priority cannot be increased, never allow a priority
    // below NORMAL.
    if (static_cast<int>(outer_->priority_hint_) <
            static_cast<int>(ThreadPriority::NORMAL) &&
        (outer_->task_tracker_->HasShutdownStarted() ||
         !PlatformThread::CanIncreaseCurrentThreadPriority())) {
      return ThreadPriority::NORMAL;
    }

    return outer_->priority_hint_;
  }

  void UpdateThreadPriority(ThreadPriority desired_thread_priority) {
    if (desired_thread_priority == current_thread_priority_)
      return;

    PlatformThread::SetCurrentThreadPriority(desired_thread_priority);
    current_thread_priority_ = desired_thread_priority;
  }

  PlatformThreadHandle thread_handle_;

  scoped_refptr<SchedulerWorker> outer_;

  // Event signaled to wake up this thread.
  WaitableEvent wake_up_event_;

  // Current priority of this thread. May be different from
  // |outer_->priority_hint_|.
  ThreadPriority current_thread_priority_;

  DISALLOW_COPY_AND_ASSIGN(Thread);
};

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
  wake_up_event->Reset();
}

SchedulerWorker::SchedulerWorker(
    ThreadPriority priority_hint,
    std::unique_ptr<Delegate> delegate,
    TaskTracker* task_tracker,
    const SchedulerLock* predecessor_lock,
    SchedulerBackwardCompatibility backward_compatibility)
    : thread_lock_(predecessor_lock),
      priority_hint_(priority_hint),
      delegate_(std::move(delegate)),
      task_tracker_(task_tracker)
#if defined(OS_WIN) && !defined(COM_INIT_CHECK_HOOK_ENABLED)
      ,
      backward_compatibility_(backward_compatibility)
#endif
{
  DCHECK(delegate_);
  DCHECK(task_tracker_);
}

bool SchedulerWorker::Start() {
  AutoSchedulerLock auto_lock(thread_lock_);
  DCHECK(!thread_);

  if (should_exit_.IsSet())
    return true;

  thread_ = Thread::Create(WrapRefCounted(this));
  return !!thread_;
}

void SchedulerWorker::WakeUp() {
  AutoSchedulerLock auto_lock(thread_lock_);

  DCHECK(!join_called_for_testing_.IsSet());
  // Calling WakeUp() after Cleanup() is wrong because the SchedulerWorker
  // cannot run more tasks.
  DCHECK(!should_exit_.IsSet());
  if (thread_)
    thread_->WakeUp();
}

void SchedulerWorker::JoinForTesting() {
  DCHECK(!join_called_for_testing_.IsSet());
  join_called_for_testing_.Set();

  std::unique_ptr<Thread> thread;

  {
    AutoSchedulerLock auto_lock(thread_lock_);

    if (thread_) {
      // Make sure the thread is awake. It will see that
      // |join_called_for_testing_| is set and exit shortly after.
      thread_->WakeUp();
      thread = std::move(thread_);
    }
  }

  if (thread)
    thread->Join();
}

bool SchedulerWorker::ThreadAliveForTesting() const {
  AutoSchedulerLock auto_lock(thread_lock_);
  return !!thread_;
}

SchedulerWorker::~SchedulerWorker() {
  if (thread_) {
    if (join_called_for_testing_.IsSet())
      return;

    DCHECK(should_exit_.IsSet());
    thread_->Detach();
  }
}

void SchedulerWorker::Cleanup() {
  AutoSchedulerLock auto_lock(thread_lock_);
  DCHECK(!should_exit_.IsSet());
  should_exit_.Set();
  if (thread_)
    thread_->WakeUp();
}

bool SchedulerWorker::ShouldExit() const {
  // The ordering of the checks is important below. This SchedulerWorker may be
  // released and outlive |task_tracker_| in unit tests. However, when the
  // SchedulerWorker is released, |should_exit_| will be set, so check that
  // first.
  return should_exit_.IsSet() || join_called_for_testing_.IsSet() ||
         task_tracker_->IsShutdownComplete();
}

}  // namespace internal
}  // namespace base
