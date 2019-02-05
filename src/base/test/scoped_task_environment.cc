// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/scoped_task_environment.h"

#include "base/bind_helpers.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/task/post_task.h"
#include "base/task/sequence_manager/sequence_manager_impl.h"
#include "base/task/sequence_manager/time_domain.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "base/task/task_scheduler/task_scheduler_impl.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/threading/sequence_local_storage_map.h"
#include "base/threading/thread_local.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
#include "base/files/file_descriptor_watcher_posix.h"
#endif

namespace base {
namespace test {

namespace {

LazyInstance<ThreadLocalPointer<ScopedTaskEnvironment::LifetimeObserver>>::Leaky
    environment_lifetime_observer;

base::Optional<MessageLoop::Type> GetMessageLoopTypeForMainThreadType(
    ScopedTaskEnvironment::MainThreadType main_thread_type) {
  switch (main_thread_type) {
    case ScopedTaskEnvironment::MainThreadType::DEFAULT:
    case ScopedTaskEnvironment::MainThreadType::MOCK_TIME:
      return MessageLoop::TYPE_DEFAULT;
    case ScopedTaskEnvironment::MainThreadType::UI:
    case ScopedTaskEnvironment::MainThreadType::UI_MOCK_TIME:
      return MessageLoop::TYPE_UI;
    case ScopedTaskEnvironment::MainThreadType::IO:
      return MessageLoop::TYPE_IO;
  }
  NOTREACHED();
  return base::nullopt;
}

std::unique_ptr<sequence_manager::SequenceManager>
CreateSequenceManagerForMainThreadType(
    ScopedTaskEnvironment::MainThreadType main_thread_type) {
  auto type = GetMessageLoopTypeForMainThreadType(main_thread_type);
  if (!type) {
    return nullptr;
  } else {
    return sequence_manager::CreateSequenceManagerOnCurrentThreadWithPump(
        *type, MessageLoop::CreateMessagePumpForType(*type));
  }
}

}  // namespace

class ScopedTaskEnvironment::MockTimeDomain
    : public sequence_manager::TimeDomain,
      public TickClock {
 public:
  MockTimeDomain() = default;
  ~MockTimeDomain() override = default;

  using TimeDomain::NextScheduledRunTime;

  static std::unique_ptr<ScopedTaskEnvironment::MockTimeDomain> Create(
      ScopedTaskEnvironment::MainThreadType main_thread_type) {
    if (main_thread_type == MainThreadType::MOCK_TIME ||
        main_thread_type == MainThreadType::UI_MOCK_TIME) {
      return std::make_unique<ScopedTaskEnvironment::MockTimeDomain>();
    }
    return nullptr;
  }

  // sequence_manager::TimeDomain:

  sequence_manager::LazyNow CreateLazyNow() const override {
    base::AutoLock lock(now_ticks_lock_);
    return sequence_manager::LazyNow(now_ticks_);
  }

  TimeTicks Now() const override {
    // This can be called from any thread.
    base::AutoLock lock(now_ticks_lock_);
    return now_ticks_;
  }

  Optional<TimeDelta> DelayTillNextTask(
      sequence_manager::LazyNow* lazy_now) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

    // Make sure TimeDomain::NextScheduledRunTime has taken canceled tasks into
    // account.
    sequence_manager()->SweepCanceledDelayedTasks();
    Optional<TimeTicks> run_time = NextScheduledRunTime();
    // Check if we've run out of tasks.
    if (!run_time)
      return base::nullopt;

    // Check if we have a task that should be running now.
    if (run_time <= now_ticks_)
      return base::TimeDelta();

    // The next task is a future delayed task. Since we're using mock time, we
    // don't want an actual OS level delayed wake up scheduled, so pretend we
    // have no more work. This will result in MaybeFastForwardToNextTask getting
    // called which lets us advance |now_ticks_|.
    return base::nullopt;
  }

  // This method is called when the underlying message pump has run out of
  // non-delayed work.
  bool MaybeFastForwardToNextTask(bool quit_when_idle_requested) override {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    // If we're being externally controlled by a RunLoop in client code, check
    // if the RunLoop is due to quit when idle, if so we don't want to advance
    // mock time.
    if (stop_when_message_pump_is_idle_ && quit_when_idle_requested)
      return false;

    // We don't need to call SweepCanceledDelayedTasks here because
    // DelayTillNextTask will have done it for us.
    Optional<TimeTicks> run_time = NextScheduledRunTime();
    if (!run_time) {
      // We've run out of tasks, but ScopedTaskEnvironment::FastForwardBy
      // requires the virtual time to be consumed.
      if (now_ticks_ < allow_advance_until_ && !allow_advance_until_.is_max())
        SetTime(allow_advance_until_);
      return false;
    }

    // Don't advance past |allow_advance_until_|.
    DCHECK_GT(*run_time, now_ticks_);
    TimeTicks time_to_advance_to = std::min(allow_advance_until_, *run_time);
    if (time_to_advance_to <= now_ticks_)
      return false;

    SetTime(time_to_advance_to);

    // Make sure a DoWork is scheduled.
    return true;
  }

  const char* GetName() const override { return "MockTimeDomain"; }

  // TickClock implementation:
  TimeTicks NowTicks() const override { return Now(); }

  // Allows time to advance when reaching idle, until
  // |now_ticks_ == advance_until|. No-op if |advance_until <= now_ticks_|.
  // Doesn't schedule work by itself.
  void SetAllowTimeToAutoAdvanceUntil(TimeTicks advance_until) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    allow_advance_until_ = advance_until;
  }

  void SetStopWhenMessagePumpIsIdle(bool stop_when_message_pump_is_idle) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    stop_when_message_pump_is_idle_ = stop_when_message_pump_is_idle;
  }

 private:
  void SetTime(TimeTicks time) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK_LE(time, allow_advance_until_);

    base::AutoLock lock(now_ticks_lock_);
    now_ticks_ = time;
  }

  SEQUENCE_CHECKER(sequence_checker_);

  // By default we want RunLoop.Run() to advance virtual time due to the API
  // contract.
  TimeTicks allow_advance_until_ = TimeTicks::Max();
  bool stop_when_message_pump_is_idle_ = true;

  // Protects |now_ticks_|
  mutable Lock now_ticks_lock_;

  // Only ever written to from the main sequence.
  TimeTicks now_ticks_;
};

class ScopedTaskEnvironment::TestTaskTracker
    : public internal::TaskSchedulerImpl::TaskTrackerImpl {
 public:
  TestTaskTracker();

  // Allow running tasks.
  void AllowRunTasks();

  // Disallow running tasks. Returns true on success; success requires there to
  // be no tasks currently running. Returns false if >0 tasks are currently
  // running. Prior to returning false, it will attempt to block until at least
  // one task has completed (in an attempt to avoid callers busy-looping
  // DisallowRunTasks() calls with the same set of slowly ongoing tasks). This
  // block attempt will also have a short timeout (in an attempt to prevent the
  // fallout of blocking: if the only task remaining is blocked on the main
  // thread, waiting for it to complete results in a deadlock...).
  bool DisallowRunTasks();

 private:
  friend class ScopedTaskEnvironment;

  // internal::TaskSchedulerImpl::TaskTrackerImpl:
  void RunOrSkipTask(internal::Task task,
                     internal::Sequence* sequence,
                     const TaskTraits& traits,
                     bool can_run_task) override;

  // Synchronizes accesses to members below.
  Lock lock_;

  // True if running tasks is allowed.
  bool can_run_tasks_ = true;

  // Signaled when |can_run_tasks_| becomes true.
  ConditionVariable can_run_tasks_cv_;

  // Signaled when a task is completed.
  ConditionVariable task_completed_;

  // Number of tasks that are currently running.
  int num_tasks_running_ = 0;

  DISALLOW_COPY_AND_ASSIGN(TestTaskTracker);
};

ScopedTaskEnvironment::ScopedTaskEnvironment(
    MainThreadType main_thread_type,
    ExecutionMode execution_control_mode)
    : ScopedTaskEnvironment(
          CreateSequenceManagerForMainThreadType(main_thread_type),
          nullptr,
          main_thread_type,
          execution_control_mode) {}

ScopedTaskEnvironment::ScopedTaskEnvironment(
    sequence_manager::SequenceManager* sequence_manager,
    MainThreadType main_thread_type,
    ExecutionMode execution_control_mode)
    : ScopedTaskEnvironment(nullptr,
                            sequence_manager,
                            main_thread_type,
                            execution_control_mode) {}

ScopedTaskEnvironment::ScopedTaskEnvironment(
    std::unique_ptr<sequence_manager::SequenceManager> owned_sequence_manager,
    sequence_manager::SequenceManager* sequence_manager,
    MainThreadType main_thread_type,
    ExecutionMode execution_control_mode)
    : execution_control_mode_(execution_control_mode),
      mock_time_domain_(MockTimeDomain::Create(main_thread_type)),
      owned_sequence_manager_(std::move(owned_sequence_manager)),
      sequence_manager_(owned_sequence_manager_.get()
                            ? owned_sequence_manager_.get()
                            : sequence_manager),
      task_queue_(CreateDefaultTaskQueue()),
#if defined(OS_POSIX) || defined(OS_FUCHSIA)
      file_descriptor_watcher_(main_thread_type == MainThreadType::IO
                                   ? std::make_unique<FileDescriptorWatcher>(
                                         task_queue_->task_runner())
                                   : nullptr),
#endif  // defined(OS_POSIX) || defined(OS_FUCHSIA)
      task_tracker_(new TestTaskTracker()) {
  CHECK(!TaskScheduler::GetInstance())
      << "Someone has already initialized TaskScheduler. If nothing in your "
         "test does so, then a test that ran earlier may have initialized one, "
         "and leaked it. base::TestSuite will trap leaked globals, unless "
         "someone has explicitly disabled it with "
         "DisableCheckForLeakedGlobals().";

  sequence_manager_->SetDefaultTaskRunner(task_queue_->task_runner());

  // Instantiate a TaskScheduler with 2 threads in each of its 4 pools. Threads
  // stay alive even when they don't have work.
  // Each pool uses two threads to prevent deadlocks in unit tests that have a
  // sequence that uses WithBaseSyncPrimitives() to wait on the result of
  // another sequence. This isn't perfect (doesn't solve wait chains) but solves
  // the basic use case for now.
  // TODO(fdoray/jeffreyhe): Make the TaskScheduler dynamically replace blocked
  // threads and get rid of this limitation. http://crbug.com/738104
  constexpr int kMaxThreads = 2;
  const TimeDelta kSuggestedReclaimTime = TimeDelta::Max();
  const SchedulerWorkerPoolParams worker_pool_params(kMaxThreads,
                                                     kSuggestedReclaimTime);
  TaskScheduler::SetInstance(std::make_unique<internal::TaskSchedulerImpl>(
      "ScopedTaskEnvironment", WrapUnique(task_tracker_)));
  task_scheduler_ = TaskScheduler::GetInstance();
  TaskScheduler::GetInstance()->Start({
    worker_pool_params, worker_pool_params, worker_pool_params,
        worker_pool_params
#if defined(OS_WIN)
        ,
        // Enable the MTA in unit tests to match the browser process'
        // TaskScheduler configuration.
        //
        // This has the adverse side-effect of enabling the MTA in non-browser
        // unit tests as well but the downside there is not as bad as not having
        // it in browser unit tests. It just means some COM asserts may pass in
        // unit tests where they wouldn't in integration tests or prod. That's
        // okay because unit tests are already generally very loose on allowing
        // I/O, waits, etc. Such misuse will still be caught in later phases
        // (and COM usage should already be pretty much inexistent in sandboxed
        // processes).
        TaskScheduler::InitParams::SharedWorkerPoolEnvironment::COM_MTA
#endif
  });

  if (execution_control_mode_ == ExecutionMode::QUEUED)
    CHECK(task_tracker_->DisallowRunTasks());

  LifetimeObserver* observer = environment_lifetime_observer.Get().Get();
  if (observer) {
    observer->OnScopedTaskEnvironmentCreated(main_thread_type,
                                             GetMainThreadTaskRunner());
  }
}

ScopedTaskEnvironment::~ScopedTaskEnvironment() {
  // Ideally this would RunLoop().RunUntilIdle() here to catch any errors or
  // infinite post loop in the remaining work but this isn't possible right now
  // because base::~MessageLoop() didn't use to do this and adding it here would
  // make the migration away from MessageLoop that much harder.
  CHECK_EQ(TaskScheduler::GetInstance(), task_scheduler_);
  // Without FlushForTesting(), DeleteSoon() and ReleaseSoon() tasks could be
  // skipped, resulting in memory leaks.
  task_tracker_->AllowRunTasks();
  TaskScheduler::GetInstance()->FlushForTesting();
  TaskScheduler::GetInstance()->Shutdown();
  TaskScheduler::GetInstance()->JoinForTesting();
  // Destroying TaskScheduler state can result in waiting on worker threads.
  // Make sure this is allowed to avoid flaking tests that have disallowed waits
  // on their main thread.
  ScopedAllowBaseSyncPrimitivesForTesting allow_waits_to_destroy_task_tracker;
  TaskScheduler::SetInstance(nullptr);

  LifetimeObserver* observer = environment_lifetime_observer.Get().Get();
  if (observer)
    observer->OnScopedTaskEnvironmentDestroyed();

  task_queue_ = nullptr;
  if (mock_time_domain_)
    sequence_manager_->UnregisterTimeDomain(mock_time_domain_.get());
}

scoped_refptr<sequence_manager::TaskQueue>
ScopedTaskEnvironment::CreateDefaultTaskQueue() {
  if (mock_time_domain_)
    sequence_manager_->RegisterTimeDomain(mock_time_domain_.get());

  return sequence_manager_->CreateTaskQueue(
      sequence_manager::TaskQueue::Spec("scoped_task_environment_default")
          .SetTimeDomain(mock_time_domain_.get()));
}

void ScopedTaskEnvironment::SetLifetimeObserver(
    ScopedTaskEnvironment::LifetimeObserver* lifetime_observer) {
  DCHECK_NE(!!environment_lifetime_observer.Get().Get(), !!lifetime_observer);
  environment_lifetime_observer.Get().Set(lifetime_observer);
}

scoped_refptr<base::SingleThreadTaskRunner>
ScopedTaskEnvironment::GetMainThreadTaskRunner() {
  return task_queue_->task_runner();
}

bool ScopedTaskEnvironment::MainThreadHasPendingTask() const {
  sequence_manager::internal::SequenceManagerImpl* sequence_manager_impl =
      static_cast<sequence_manager::internal::SequenceManagerImpl*>(
          sequence_manager_);
  sequence_manager_impl->SweepCanceledDelayedTasks();
  // Unfortunately this API means different things depending on whether mock
  // time is used or not. If MockTime is used then tests want to know if there
  // are any delayed or non-delayed tasks, otherwise only non-delayed tasks are
  // considered.
  if (mock_time_domain_)
    return sequence_manager_impl->HasTasks();
  return !sequence_manager_impl->IsIdleForTesting();
}

void ScopedTaskEnvironment::RunUntilIdle() {
  // Prevent virtual time from advancing while within this call.
  if (mock_time_domain_)
    mock_time_domain_->SetAllowTimeToAutoAdvanceUntil(TimeTicks());

  // TODO(gab): This can be heavily simplified to essentially:
  //     bool HasMainThreadTasks() {
  //      if (message_loop_)
  //        return !message_loop_->IsIdleForTesting();
  //      return mock_time_task_runner_->NextPendingTaskDelay().is_zero();
  //     }
  //     while (task_tracker_->HasIncompleteTasks() || HasMainThreadTasks()) {
  //       base::RunLoop().RunUntilIdle();
  //       // Avoid busy-looping.
  //       if (task_tracker_->HasIncompleteTasks())
  //         PlatformThread::Sleep(TimeDelta::FromMilliSeconds(1));
  //     }
  // Update: This can likely be done now that MessageLoop::IsIdleForTesting()
  // checks all queues.
  //
  // Other than that it works because once |task_tracker_->HasIncompleteTasks()|
  // is false we know for sure that the only thing that can make it true is a
  // main thread task (ScopedTaskEnvironment owns all the threads). As such we
  // can't racily see it as false on the main thread and be wrong as if it the
  // main thread sees the atomic count at zero, it's the only one that can make
  // it go up. And the only thing that can make it go up on the main thread are
  // main thread tasks and therefore we're done if there aren't any left.
  //
  // This simplification further allows simplification of DisallowRunTasks().
  //
  // This can also be simplified even further once TaskTracker becomes directly
  // aware of main thread tasks. https://crbug.com/660078.

  for (;;) {
    task_tracker_->AllowRunTasks();

    // First run as many tasks as possible on the main thread in parallel with
    // tasks in TaskScheduler. This increases likelihood of TSAN catching
    // threading errors and eliminates possibility of hangs should a
    // TaskScheduler task synchronously block on a main thread task
    // (TaskScheduler::FlushForTesting() can't be used here for that reason).
    RunLoop().RunUntilIdle();

    // Then halt TaskScheduler. DisallowRunTasks() failing indicates that there
    // were TaskScheduler tasks currently running. In that case, try again from
    // top when DisallowRunTasks() yields control back to this thread as they
    // may have posted main thread tasks.
    if (!task_tracker_->DisallowRunTasks())
      continue;

    // Once TaskScheduler is halted. Run any remaining main thread tasks (which
    // may have been posted by TaskScheduler tasks that completed between the
    // above main thread RunUntilIdle() and TaskScheduler DisallowRunTasks()).
    // Note: this assumes that no main thread task synchronously blocks on a
    // TaskScheduler tasks (it certainly shouldn't); this call could otherwise
    // hang.
    RunLoop().RunUntilIdle();

    // The above RunUntilIdle() guarantees there are no remaining main thread
    // tasks (the TaskScheduler being halted during the last RunUntilIdle() is
    // key as it prevents a task being posted to it racily with it determining
    // it had no work remaining). Therefore, we're done if there is no more work
    // on TaskScheduler either (there can be TaskScheduler work remaining if
    // DisallowRunTasks() preempted work and/or the last RunUntilIdle() posted
    // more TaskScheduler tasks).
    // Note: this last |if| couldn't be turned into a |do {} while();|. A
    // conditional loop makes it such that |continue;| results in checking the
    // condition (not unconditionally loop again) which would be incorrect for
    // the above logic as it'd then be possible for a TaskScheduler task to be
    // running during the DisallowRunTasks() test, causing it to fail, but then
    // post to the main thread and complete before the loop's condition is
    // verified which could result in HasIncompleteUndelayedTasksForTesting()
    // returning false and the loop erroneously exiting with a pending task on
    // the main thread.
    if (!task_tracker_->HasIncompleteUndelayedTasksForTesting())
      break;
  }

  // The above loop always ends with running tasks being disallowed. Re-enable
  // parallel execution before returning unless in ExecutionMode::QUEUED.
  if (execution_control_mode_ != ExecutionMode::QUEUED)
    task_tracker_->AllowRunTasks();

  if (mock_time_domain_)
    mock_time_domain_->SetAllowTimeToAutoAdvanceUntil(TimeTicks::Max());
}

void ScopedTaskEnvironment::FastForwardBy(TimeDelta delta) {
  MessageLoopCurrent::ScopedNestableTaskAllower allow;
  DCHECK(mock_time_domain_);
  mock_time_domain_->SetStopWhenMessagePumpIsIdle(false);
  mock_time_domain_->SetAllowTimeToAutoAdvanceUntil(mock_time_domain_->Now() +
                                                    delta);
  RunLoop().RunUntilIdle();
  mock_time_domain_->SetStopWhenMessagePumpIsIdle(true);
  mock_time_domain_->SetAllowTimeToAutoAdvanceUntil(TimeTicks::Max());
}

void ScopedTaskEnvironment::FastForwardUntilNoTasksRemain() {
  // TimeTicks::operator+(TimeDelta) uses saturated arithmetic so it's safe to
  // pass in TimeDelta::Max().
  FastForwardBy(TimeDelta::Max());
}

const TickClock* ScopedTaskEnvironment::GetMockTickClock() {
  DCHECK(mock_time_domain_);
  return mock_time_domain_.get();
}

base::TimeTicks ScopedTaskEnvironment::NowTicks() const {
  DCHECK(mock_time_domain_);
  return mock_time_domain_->Now();
}

size_t ScopedTaskEnvironment::GetPendingMainThreadTaskCount() const {
  sequence_manager_->SweepCanceledDelayedTasks();
  return sequence_manager_->GetPendingTaskCountForTesting();
}

TimeDelta ScopedTaskEnvironment::NextMainThreadPendingTaskDelay() const {
  sequence_manager_->SweepCanceledDelayedTasks();
  DCHECK(mock_time_domain_);
  Optional<TimeTicks> run_time = mock_time_domain_->NextScheduledRunTime();
  if (run_time)
    return *run_time - mock_time_domain_->Now();
  return TimeDelta::Max();
}

ScopedTaskEnvironment::TestTaskTracker::TestTaskTracker()
    : internal::TaskSchedulerImpl::TaskTrackerImpl("ScopedTaskEnvironment"),
      can_run_tasks_cv_(&lock_),
      task_completed_(&lock_) {}

void ScopedTaskEnvironment::TestTaskTracker::AllowRunTasks() {
  AutoLock auto_lock(lock_);
  can_run_tasks_ = true;
  can_run_tasks_cv_.Broadcast();
}

bool ScopedTaskEnvironment::TestTaskTracker::DisallowRunTasks() {
  AutoLock auto_lock(lock_);

  // Can't disallow run task if there are tasks running.
  if (num_tasks_running_ > 0) {
    // Attempt to wait a bit so that the caller doesn't busy-loop with the same
    // set of pending work. A short wait is required to avoid deadlock
    // scenarios. See DisallowRunTasks()'s declaration for more details.
    task_completed_.TimedWait(TimeDelta::FromMilliseconds(1));
    return false;
  }

  can_run_tasks_ = false;
  return true;
}

void ScopedTaskEnvironment::TestTaskTracker::RunOrSkipTask(
    internal::Task task,
    internal::Sequence* sequence,
    const TaskTraits& traits,
    bool can_run_task) {
  {
    AutoLock auto_lock(lock_);

    while (!can_run_tasks_)
      can_run_tasks_cv_.Wait();

    ++num_tasks_running_;
  }

  internal::TaskSchedulerImpl::TaskTrackerImpl::RunOrSkipTask(
      std::move(task), sequence, traits, can_run_task);

  {
    AutoLock auto_lock(lock_);

    CHECK_GT(num_tasks_running_, 0);
    CHECK(can_run_tasks_);

    --num_tasks_running_;

    task_completed_.Broadcast();
  }
}

}  // namespace test
}  // namespace base
