// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TASK_SCHEDULER_SCHEDULER_WORKER_H_
#define BASE_TASK_SCHEDULER_SCHEDULER_WORKER_H_

#include <memory>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/synchronization/atomic_flag.h"
#include "base/synchronization/waitable_event.h"
#include "base/task_scheduler/can_schedule_sequence_observer.h"
#include "base/task_scheduler/scheduler_lock.h"
#include "base/task_scheduler/scheduler_worker_params.h"
#include "base/task_scheduler/sequence.h"
#include "base/threading/platform_thread.h"
#include "base/time/time.h"
#include "build/build_config.h"

#if defined(OS_WIN)
#include "base/win/com_init_check_hook.h"
#endif

namespace base {
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
    : public RefCountedThreadSafe<SchedulerWorker> {
 public:
  // Delegate interface for SchedulerWorker. All methods except
  // OnCanScheduleSequence() (inherited from CanScheduleSequenceObserver) are
  // called from the thread managed by the SchedulerWorker instance.
  class BASE_EXPORT Delegate : public CanScheduleSequenceObserver {
   public:
    ~Delegate() override = default;

    // Called by |worker|'s thread when it enters its main function.
    virtual void OnMainEntry(SchedulerWorker* worker) = 0;

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

    // Called by |worker|'s thread right before the main function exits.
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
                  TaskTracker* task_tracker,
                  const SchedulerLock* predecessor_lock = nullptr,
                  SchedulerBackwardCompatibility backward_compatibility =
                      SchedulerBackwardCompatibility::DISABLED);

  // Creates a thread to back the SchedulerWorker. The thread will be in a wait
  // state pending a WakeUp() call. No thread will be created if Cleanup() was
  // called. Returns true on success.
  bool Start();

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

 private:
  friend class RefCountedThreadSafe<SchedulerWorker>;
  class Thread;

  ~SchedulerWorker();

  bool ShouldExit() const;

  // Synchronizes access to |thread_| (read+write) and |started_| (read+write).
  mutable SchedulerLock thread_lock_;

  // The underlying thread for this SchedulerWorker.
  // The thread object will be cleaned up by the running thread unless we join
  // against the thread. Joining requires the thread object to remain alive for
  // the Thread::Join() call.
  std::unique_ptr<Thread> thread_;

  AtomicFlag should_exit_;

  const ThreadPriority priority_hint_;

  const std::unique_ptr<Delegate> delegate_;
  TaskTracker* const task_tracker_;

#if defined(OS_WIN) && !defined(COM_INIT_CHECK_HOOK_ENABLED)
  const SchedulerBackwardCompatibility backward_compatibility_;
#endif

  // Set once JoinForTesting() has been called.
  AtomicFlag join_called_for_testing_;

  DISALLOW_COPY_AND_ASSIGN(SchedulerWorker);
};

}  // namespace internal
}  // namespace base

#endif  // BASE_TASK_SCHEDULER_SCHEDULER_WORKER_H_
