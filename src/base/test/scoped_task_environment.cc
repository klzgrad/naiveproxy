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
#include "base/test/bind_test_util.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/test/test_timeouts.h"
#include "base/threading/sequence_local_storage_map.h"
#include "base/threading/thread_local.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "base/time/time_override.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
#include "base/files/file_descriptor_watcher_posix.h"
#endif

namespace base {
namespace test {

namespace {

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
    case ScopedTaskEnvironment::MainThreadType::IO_MOCK_TIME:
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
    auto settings = base::sequence_manager::SequenceManager::Settings{
        .message_loop_type = *type};
    return sequence_manager::CreateSequenceManagerOnCurrentThreadWithPump(
        MessageLoop::CreateMessagePumpForType(*type), std::move(settings));
  }
}

class TickClockBasedClock : public Clock {
 public:
  explicit TickClockBasedClock(const TickClock* tick_clock)
      : tick_clock_(*tick_clock),
        start_ticks_(tick_clock_.NowTicks()),
        start_time_(Time::UnixEpoch()) {}

  Time Now() const override {
    return start_time_ + (tick_clock_.NowTicks() - start_ticks_);
  }

 private:
  const TickClock& tick_clock_;
  const TimeTicks start_ticks_;
  const Time start_time_;
};

}  // namespace

class ScopedTaskEnvironment::MockTimeDomain
    : public sequence_manager::TimeDomain,
      public TickClock {
 public:
  MockTimeDomain(ScopedTaskEnvironment::NowSource now_source,
                 sequence_manager::SequenceManager* sequence_manager)
      : sequence_manager_(sequence_manager) {
    DCHECK_EQ(nullptr, current_mock_time_domain_);
    current_mock_time_domain_ = this;
    if (now_source == ScopedTaskEnvironment::NowSource::MAIN_THREAD_MOCK_TIME) {
      time_overrides_ = std::make_unique<subtle::ScopedTimeClockOverrides>(
          &MockTimeDomain::GetTime, &MockTimeDomain::GetTimeTicks, nullptr);
    }
  }

  ~MockTimeDomain() override {
    DCHECK_EQ(this, current_mock_time_domain_);
    current_mock_time_domain_ = nullptr;
  }

  static MockTimeDomain* current_mock_time_domain_;

  static Time GetTime() {
    return Time::UnixEpoch() + (current_mock_time_domain_->Now() - TimeTicks());
  }

  static TimeTicks GetTimeTicks() { return current_mock_time_domain_->Now(); }

  using TimeDomain::NextScheduledRunTime;

  Optional<TimeTicks> NextScheduledRunTime() const {
    // The TimeDomain doesn't know about immediate tasks, check if we have any.
    if (!sequence_manager_->IsIdleForTesting())
      return Now();
    return TimeDomain::NextScheduledRunTime();
  }

  static std::unique_ptr<ScopedTaskEnvironment::MockTimeDomain>
  CreateAndRegister(ScopedTaskEnvironment::MainThreadType main_thread_type,
                    ScopedTaskEnvironment::NowSource now_source,
                    sequence_manager::SequenceManager* sequence_manager) {
    if (main_thread_type == MainThreadType::MOCK_TIME ||
        main_thread_type == MainThreadType::UI_MOCK_TIME ||
        main_thread_type == MainThreadType::IO_MOCK_TIME) {
      auto mock_time_donain =
          std::make_unique<ScopedTaskEnvironment::MockTimeDomain>(
              now_source, sequence_manager);
      sequence_manager->RegisterTimeDomain(mock_time_donain.get());
      return mock_time_donain;
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
    // account, ReclaimMemory sweeps canceled delayed tasks.
    sequence_manager()->ReclaimMemory();
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

    // We don't need to call ReclaimMemory here because
    // DelayTillNextTask will have dealt with cancelled delayed tasks for us.
    Optional<TimeTicks> run_time = NextScheduledRunTime();
    if (!run_time || run_time == now_ticks_) {
      // We've run out of tasks (or an immediate task came in racily from
      // another thread after reaching idle, ignore it, it will be processed in
      // the next run as-if it arrived slightly later).
      // ScopedTaskEnvironment::FastForwardBy requires the remaining virtual
      // time to be consumed upon reaching idle.
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

  sequence_manager::SequenceManager* const sequence_manager_;

  std::unique_ptr<subtle::ScopedTimeClockOverrides> time_overrides_;

  // By default we want RunLoop.Run() to advance virtual time due to the API
  // contract.
  TimeTicks allow_advance_until_ = TimeTicks::Max();
  bool stop_when_message_pump_is_idle_ = true;

  // Protects |now_ticks_|
  mutable Lock now_ticks_lock_;

  // Only ever written to from the main sequence.
  // TODO(alexclarke): We can't override now by default with now_ticks_ staring
  // from zero because many tests have checks that TimeTicks::Now() returns a
  // non-zero result.
  TimeTicks now_ticks_;
};

ScopedTaskEnvironment::MockTimeDomain*
    ScopedTaskEnvironment::MockTimeDomain::current_mock_time_domain_ = nullptr;

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
    ExecutionMode execution_control_mode,
    NowSource now_source,
    bool subclass_creates_default_taskrunner,
    trait_helpers::NotATraitTag)
    : main_thread_type_(main_thread_type),
      execution_control_mode_(execution_control_mode),
      subclass_creates_default_taskrunner_(subclass_creates_default_taskrunner),
      sequence_manager_(
          CreateSequenceManagerForMainThreadType(main_thread_type)),
      mock_time_domain_(
          MockTimeDomain::CreateAndRegister(main_thread_type,
                                            now_source,
                                            sequence_manager_.get())),
      mock_clock_(mock_time_domain_ ? std::make_unique<TickClockBasedClock>(
                                          mock_time_domain_.get())
                                    : nullptr),
      task_tracker_(new TestTaskTracker()),
      scoped_lazy_task_runner_list_for_testing_(
          std::make_unique<internal::ScopedLazyTaskRunnerListForTesting>()),
      // TODO(https://crbug.com/918724): Enable Run() timeouts even for
      // instances created with *MOCK_TIME, and determine whether the timeout
      // can be reduced from action_max_timeout() to action_timeout().
      run_loop_timeout_(
          mock_time_domain_
              ? nullptr
              : std::make_unique<RunLoop::ScopedRunTimeoutForTest>(
                    TestTimeouts::action_max_timeout(),
                    MakeExpectedNotRunClosure(FROM_HERE, "Run() timed out."))) {
  CHECK(now_source == NowSource::REAL_TIME || mock_time_domain_)
      << "NowSource must be REAL_TIME unless we're using mock time";
  CHECK(!TaskScheduler::GetInstance())
      << "Someone has already initialized TaskScheduler. If nothing in your "
         "test does so, then a test that ran earlier may have initialized one, "
         "and leaked it. base::TestSuite will trap leaked globals, unless "
         "someone has explicitly disabled it with "
         "DisableCheckForLeakedGlobals().";

  CHECK(!base::ThreadTaskRunnerHandle::IsSet());
  // If |subclass_creates_default_taskrunner| is true then initialization is
  // deferred until DeferredInitFromSubclass().
  if (!subclass_creates_default_taskrunner) {
    task_queue_ = sequence_manager_->CreateTaskQueue(
        sequence_manager::TaskQueue::Spec("scoped_task_environment_default")
            .SetTimeDomain(mock_time_domain_.get()));
    task_runner_ = task_queue_->task_runner();
    sequence_manager_->SetDefaultTaskRunner(task_runner_);
    CHECK(base::ThreadTaskRunnerHandle::IsSet())
        << "ThreadTaskRunnerHandle should've been set now.";
    CompleteInitialization();
  }

  // Instantiate a TaskScheduler with 4 workers per pool. Having multiple
  // threads prevents deadlocks should some blocking APIs not use
  // ScopedBlockingCall. It also allows enough concurrency to allow TSAN to spot
  // data races.
  constexpr int kMaxThreads = 4;
  const TimeDelta kSuggestedReclaimTime = TimeDelta::Max();
  const SchedulerWorkerPoolParams worker_pool_params(kMaxThreads,
                                                     kSuggestedReclaimTime);
  TaskScheduler::SetInstance(std::make_unique<internal::TaskSchedulerImpl>(
      "ScopedTaskEnvironment", WrapUnique(task_tracker_)));
  task_scheduler_ = TaskScheduler::GetInstance();
  TaskScheduler::GetInstance()->Start({
    worker_pool_params, worker_pool_params
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
}

void ScopedTaskEnvironment::CompleteInitialization() {
#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  if (main_thread_type() == MainThreadType::IO) {
    file_descriptor_watcher_ =
        std::make_unique<FileDescriptorWatcher>(GetMainThreadTaskRunner());
  }
#endif  // defined(OS_POSIX) || defined(OS_FUCHSIA)
}

ScopedTaskEnvironment::ScopedTaskEnvironment(ScopedTaskEnvironment&& other) =
    default;

ScopedTaskEnvironment::~ScopedTaskEnvironment() {
  // If we've been moved then bail out.
  if (!owns_instance_)
    return;

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
  task_queue_ = nullptr;
  NotifyDestructionObserversAndReleaseSequenceManager();
}

sequence_manager::TimeDomain* ScopedTaskEnvironment::GetTimeDomain() const {
  return mock_time_domain_ ? mock_time_domain_.get()
                           : sequence_manager_->GetRealTimeDomain();
}

sequence_manager::SequenceManager* ScopedTaskEnvironment::sequence_manager()
    const {
  DCHECK(subclass_creates_default_taskrunner_);
  return sequence_manager_.get();
}

void ScopedTaskEnvironment::DeferredInitFromSubclass(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  task_runner_ = std::move(task_runner);
  sequence_manager_->SetDefaultTaskRunner(task_runner_);
  CompleteInitialization();
}

void ScopedTaskEnvironment::
    NotifyDestructionObserversAndReleaseSequenceManager() {
  // A derived classes may call this method early.
  if (!sequence_manager_)
    return;

  if (mock_time_domain_)
    sequence_manager_->UnregisterTimeDomain(mock_time_domain_.get());

  sequence_manager_.reset();
}

scoped_refptr<base::SingleThreadTaskRunner>
ScopedTaskEnvironment::GetMainThreadTaskRunner() {
  DCHECK(task_runner_);
  return task_runner_;
}

bool ScopedTaskEnvironment::MainThreadIsIdle() const {
  sequence_manager::internal::SequenceManagerImpl* sequence_manager_impl =
      static_cast<sequence_manager::internal::SequenceManagerImpl*>(
          sequence_manager_.get());
  // ReclaimMemory sweeps canceled delayed tasks.
  sequence_manager_impl->ReclaimMemory();
  return sequence_manager_impl->IsIdleForTesting();
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

const TickClock* ScopedTaskEnvironment::GetMockTickClock() const {
  DCHECK(mock_time_domain_);
  return mock_time_domain_.get();
}

base::TimeTicks ScopedTaskEnvironment::NowTicks() const {
  DCHECK(mock_time_domain_);
  return mock_time_domain_->Now();
}

const Clock* ScopedTaskEnvironment::GetMockClock() const {
  DCHECK(mock_clock_);
  return mock_clock_.get();
}

size_t ScopedTaskEnvironment::GetPendingMainThreadTaskCount() const {
  // ReclaimMemory sweeps canceled delayed tasks.
  sequence_manager_->ReclaimMemory();
  return sequence_manager_->GetPendingTaskCountForTesting();
}

TimeDelta ScopedTaskEnvironment::NextMainThreadPendingTaskDelay() const {
  // ReclaimMemory sweeps canceled delayed tasks.
  sequence_manager_->ReclaimMemory();
  DCHECK(mock_time_domain_);
  Optional<TimeTicks> run_time = mock_time_domain_->NextScheduledRunTime();
  if (run_time)
    return *run_time - mock_time_domain_->Now();
  return TimeDelta::Max();
}

bool ScopedTaskEnvironment::NextTaskIsDelayed() const {
  TimeDelta delay = NextMainThreadPendingTaskDelay();
  return !delay.is_zero() && !delay.is_max();
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
