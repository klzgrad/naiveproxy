// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_H_
#define BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_H_

#include <memory>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/atomic_flag.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/task_scheduler/can_schedule_sequence_observer.h"
#include "base/task/task_scheduler/scheduler_lock.h"
#include "base/task/task_scheduler/scheduler_worker_params.h"
#include "base/task/task_scheduler/sequence.h"
#include "base/task/task_scheduler/tracked_ref.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"

#if defined(OS_WIN)
#include "base/win/com_init_check_hook.h"
#endif

namespace base {

class SchedulerWorkerObserver;

namespace internal {

class TaskTracker;

// A worker that manages a single thread to run Tasks from Sequences returned
// by a delegate.
//
// A SchedulerWorker starts out sleeping. It is woken up by a call to WakeUp().
// After a wake-up, a SchedulerWorker runs Tasks from Sequences returned by the
// GetWork() method of its delegate as long as it doesn't return nullptr. It
// also periodically checks with its TaskTracker whether shutdown has completed
// and exits when it has.
//
// This class is thread-safe.
class BASE_EXPORT SchedulerWorker
    : public RefCountedThreadSafe<SchedulerWorker>,
      public PlatformThread::Delegate {
 public:
  // Labels this SchedulerWorker's association. This doesn't affect any logic
  // but will add a stack frame labeling this thread for ease of stack trace
  // identification.
  enum class ThreadLabel {
    POOLED,
    SHARED,
    DEDICATED,
#if defined(OS_WIN)
    SHARED_COM,
    DEDICATED_COM,
#endif  // defined(OS_WIN)
  };

  // Delegate interface for SchedulerWorker. All methods except
  // OnCanScheduleSequence() (inherited from CanScheduleSequenceObserver) are
  // called from the thread managed by the SchedulerWorker instance.
  class BASE_EXPORT Delegate : public CanScheduleSequenceObserver {
   public:
    ~Delegate() override = default;

    // Returns the ThreadLabel the Delegate wants its SchedulerWorkers' stacks
    // to be labeled with.
    virtual ThreadLabel GetThreadLabel() const = 0;

    // Called by |worker|'s thread when it enters its main function.
    virtual void OnMainEntry(const SchedulerWorker* worker) = 0;

    // Called by |worker|'s thread to get a Sequence from which to run a Task.
    virtual scoped_refptr<Sequence> GetWork(SchedulerWorker* worker) = 0;

    // Called by the SchedulerWorker after it ran a task.
    virtual void DidRunTask() = 0;

    // Called when |sequence| isn't empty after the SchedulerWorker pops a Task
    // from it. |sequence| is the last Sequence returned by GetWork().
    //
    // TODO(fdoray): Rename to RescheduleSequence() to match TaskTracker
    // terminology.
    virtual void ReEnqueueSequence(scoped_refptr<Sequence> sequence) = 0;

    // Called to determine how long to sleep before the next call to GetWork().
    // GetWork() may be called before this timeout expires if the worker's
    // WakeUp() method is called.
    virtual TimeDelta GetSleepTimeout() = 0;

    // Called by the SchedulerWorker's thread to wait for work. Override this
    // method if the thread in question needs special handling to go to sleep.
    // |wake_up_event| is a manually resettable event and is signaled on
    // SchedulerWorker::WakeUp()
    virtual void WaitForWork(WaitableEvent* wake_up_event);

    // Called by |worker|'s thread right before the main function exits. The
    // Delegate is free to release any associated resources in this call. It is
    // guaranteed that SchedulerWorker won't access the Delegate or the
    // TaskTracker after calling OnMainExit() on the Delegate.
    virtual void OnMainExit(SchedulerWorker* worker) {}
  };

  // Creates a SchedulerWorker that runs Tasks from Sequences returned by
  // |delegate|. No actual thread will be created for this SchedulerWorker
  // before Start() is called. |priority_hint| is the preferred thread priority;
  // the actual thread priority depends on shutdown state and platform
  // capabilities. |task_tracker| is used to handle shutdown behavior of Tasks.
  // |predecessor_lock| is a lock that is allowed to be held when calling
  // methods on this SchedulerWorker. |backward_compatibility| indicates
  // whether backward compatibility is enabled. Either JoinForTesting() or
  // Cleanup() must be called before releasing the last external reference.
  SchedulerWorker(ThreadPriority priority_hint,
                  std::unique_ptr<Delegate> delegate,
                  TrackedRef<TaskTracker> task_tracker,
                  const SchedulerLock* predecessor_lock = nullptr,
                  SchedulerBackwardCompatibility backward_compatibility =
                      SchedulerBackwardCompatibility::DISABLED);

  // Creates a thread to back the SchedulerWorker. The thread will be in a wait
  // state pending a WakeUp() call. No thread will be created if Cleanup() was
  // called. If specified, |scheduler_worker_observer| will be notified when the
  // worker enters and exits its main function. It must not be destroyed before
  // JoinForTesting() has returned (must never be destroyed in production).
  // Returns true on success.
  bool Start(SchedulerWorkerObserver* scheduler_worker_observer = nullptr);

  // Wakes up this SchedulerWorker if it wasn't already awake. After this is
  // called, this SchedulerWorker will run Tasks from Sequences returned by the
  // GetWork() method of its delegate until it returns nullptr. No-op if Start()
  // wasn't called. DCHECKs if called after Start() has failed or after
  // Cleanup() has been called.
  void WakeUp();

  SchedulerWorker::Delegate* delegate() { return delegate_.get(); }

  // Joins this SchedulerWorker. If a Task is already running, it will be
  // allowed to complete its execution. This can only be called once.
  //
  // Note: A thread that detaches before JoinForTesting() is called may still be
  // running after JoinForTesting() returns. However, it can't run tasks after
  // JoinForTesting() returns.
  void JoinForTesting();

  // Returns true if the worker is alive.
  bool ThreadAliveForTesting() const;

  // Makes a request to cleanup the worker. This may be called from any thread.
  // The caller is expected to release its reference to this object after
  // calling Cleanup(). Further method calls after Cleanup() returns are
  // undefined.
  //
  // Expected Usage:
  //   scoped_refptr<SchedulerWorker> worker_ = /* Existing Worker */
  //   worker_->Cleanup();
  //   worker_ = nullptr;
  void Cleanup();

  // Informs this SchedulerWorker about periods during which it is not being
  // used. Thread-safe.
  void BeginUnusedPeriod();
  void EndUnusedPeriod();
  // Returns the last time this SchedulerWorker was used. Returns a null time if
  // this SchedulerWorker is currently in-use. Thread-safe.
  TimeTicks GetLastUsedTime() const;

 private:
  friend class RefCountedThreadSafe<SchedulerWorker>;
  class Thread;

  ~SchedulerWorker() override;

  bool ShouldExit() const;

  // Returns the thread priority to use based on the priority hint, current
  // shutdown state, and platform capabilities.
  ThreadPriority GetDesiredThreadPriority() const;

  // Changes the thread priority to |desired_thread_priority|. Must be called on
  // the thread managed by |this|.
  void UpdateThreadPriority(ThreadPriority desired_thread_priority);

  // PlatformThread::Delegate:
  void ThreadMain() override;

  // Dummy frames to act as "RunLabeledWorker()" (see RunMain() below). Their
  // impl is aliased to prevent compiler/linker from optimizing them out.
  void RunPooledWorker();
  void RunBackgroundPooledWorker();
  void RunSharedWorker();
  void RunBackgroundSharedWorker();
  void RunDedicatedWorker();
  void RunBackgroundDedicatedWorker();
#if defined(OS_WIN)
  void RunSharedCOMWorker();
  void RunBackgroundSharedCOMWorker();
  void RunDedicatedCOMWorker();
  void RunBackgroundDedicatedCOMWorker();
#endif  // defined(OS_WIN)

  // The real main, invoked through :
  //     ThreadMain() -> RunLabeledWorker() -> RunWorker().
  // "RunLabeledWorker()" is a dummy frame based on ThreadLabel+ThreadPriority
  // and used to easily identify threads in stack traces.
  void RunWorker();

  // Self-reference to prevent destruction of |this| while the thread is alive.
  // Set in Start() before creating the thread. Reset in ThreadMain() before the
  // thread exits. No lock required because the first access occurs before the
  // thread is created and the second access occurs on the thread.
  scoped_refptr<SchedulerWorker> self_;

  // Synchronizes access to |thread_handle_| and |last_used_time_|.
  mutable SchedulerLock thread_lock_;

  // Handle for the thread managed by |this|.
  PlatformThreadHandle thread_handle_;

  // The last time this worker was used by its owner (e.g. to process work or
  // stand as a required idle thread).
  TimeTicks last_used_time_;

  // Event to wake up the thread managed by |this|.
  WaitableEvent wake_up_event_{WaitableEvent::ResetPolicy::AUTOMATIC,
                               WaitableEvent::InitialState::NOT_SIGNALED};

  // Whether the thread should exit. Set by Cleanup().
  AtomicFlag should_exit_;

  const std::unique_ptr<Delegate> delegate_;
  const TrackedRef<TaskTracker> task_tracker_;

  // Optional observer notified when a worker enters and exits its main
  // function. Set in Start() and never modified afterwards.
  SchedulerWorkerObserver* scheduler_worker_observer_ = nullptr;

  // Desired thread priority.
  const ThreadPriority priority_hint_;

  // Actual thread priority. Can be different than |priority_hint_| depending on
  // system capabilities and shutdown state. No lock required because all post-
  // construction accesses occur on the thread.
  ThreadPriority current_thread_priority_;

#if defined(OS_WIN) && !defined(COM_INIT_CHECK_HOOK_ENABLED)
  const SchedulerBackwardCompatibility backward_compatibility_;
#endif

  // Set once JoinForTesting() has been called.
  AtomicFlag join_called_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorker);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_TASK_SCHEDULER_SCHEDULER_WORKER_H_
