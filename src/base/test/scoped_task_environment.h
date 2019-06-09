// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_TEST_SCOPED_TASK_ENVIRONMENT_H_
#define BASE_TEST_SCOPED_TASK_ENVIRONMENT_H_

#include <memory>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/task/lazy_task_runner.h"
#include "base/task/sequence_manager/sequence_manager.h"
#include "base/time/time.h"
#include "base/traits_bag.h"
#include "build/build_config.h"

namespace base {

class Clock;
class FileDescriptorWatcher;
class ThreadPoolInstance;
class TickClock;

namespace test {

// ScopedTaskEnvironment allows usage of these APIs within its scope:
// - (Thread|Sequenced)TaskRunnerHandle, on the thread where it lives
// - base/task/post_task.h, on any thread
//
// Tests that need either of these APIs should instantiate a
// ScopedTaskEnvironment.
//
// Tasks posted to the (Thread|Sequenced)TaskRunnerHandle run synchronously when
// RunLoop::Run(UntilIdle) or ScopedTaskEnvironment::RunUntilIdle is called on
// the thread where the ScopedTaskEnvironment lives.
//
// Tasks posted through base/task/post_task.h run on dedicated threads. If
// ThreadPoolExecutionMode is QUEUED, they run when RunUntilIdle() or
// ~ScopedTaskEnvironment is called. If ThreadPoolExecutionMode is ASYNC, they
// run as they are posted.
//
// All methods of ScopedTaskEnvironment must be called from the same thread.
//
// Usage:
//
//   class MyTestFixture : public testing::Test {
//    public:
//     (...)
//
//    protected:
//     // Must be the first member (or at least before any member that cares
//     // about tasks) to be initialized first and destroyed last. protected
//     // instead of private visibility will allow controlling the task
//     // environment (e.g. clock) once such features are added (see design doc
//     // below for details), until then it at least doesn't hurt :).
//     base::test::ScopedTaskEnvironment scoped_task_environment_;
//
//     // Other members go here (or further below in private section.)
//   };
//
// Design and future improvements documented in
// https://docs.google.com/document/d/1QabRo8c7D9LsYY3cEcaPQbOCLo8Tu-6VLykYXyl3Pkk/edit
class ScopedTaskEnvironment {
 protected:
  // This enables a two-phase initialization for sub classes such as
  // content::TestBrowserThreadBundle which need to provide the default task
  // queue because they instantiate a scheduler on the same thread. Subclasses
  // using this trait must invoke DeferredInitFromSubclass() before running the
  // task environment.
  struct SubclassCreatesDefaultTaskRunner {};

 public:
  enum class MainThreadType {
    // The main thread doesn't pump system messages.
    DEFAULT,
    // The main thread doesn't pump system messages and uses a mock clock for
    // delayed tasks (controllable via FastForward*() methods).
    // TODO(gab): Make this the default |main_thread_type|.
    // TODO(gab): Also mock the ThreadPoolInstance's clock simultaneously (this
    // currently only mocks the main thread's clock).
    MOCK_TIME,
    // The main thread pumps UI messages.
    UI,
    // The main thread pumps UI messages and uses a mock clock for delayed tasks
    // (controllable via FastForward*() methods).
    // TODO(gab@): Enable mock time on all threads and make MOCK_TIME
    // configurable independent of MainThreadType.
    UI_MOCK_TIME,
    // The main thread pumps asynchronous IO messages and supports the
    // FileDescriptorWatcher API on POSIX.
    IO,
    // The main thread pumps IO messages and uses a mock clock for delayed tasks
    // (controllable via FastForward*() methods). In addition it supports the
    // FileDescriptorWatcher API on POSIX.
    IO_MOCK_TIME,
  };

  // Note that this is irrelevant (and ignored) under
  // ThreadingMode::MAIN_THREAD_ONLY
  enum class ThreadPoolExecutionMode {
    // Thread pool tasks are queued and only executed when RunUntilIdle() is
    // explicitly
    // called.
    QUEUED,
    // Thread pool tasks run as they are posted. RunUntilIdle() can still be
    // used to block
    // until done.
    ASYNC,
    DEFAULT = ASYNC
  };

  // TODO(carlscab): Deprecated. Migrate all uses and remove.
  using ExecutionMode = ThreadPoolExecutionMode;

  enum class NowSource {
    // base::Time::Now() and base::TimeTicks::Now() are real time.
    REAL_TIME,

    // base::Time::Now() and base::TimeTicks::Now() are driven from the main
    // thread's MOCK_TIME. This may alter the order of delayed and non-delayed
    // tasks on other threads.
    //
    // Warning some platform APIs are still real time, and don't interact with
    // MOCK_TIME as expected, e.g.:
    //   PlatformThread::Sleep
    //   WaitableEvent::TimedWait
    //   WaitableEvent::TimedWaitUntil
    //   ConditionVariable::TimedWait
    MAIN_THREAD_MOCK_TIME,
  };

  enum class ThreadingMode {
    // ThreadPool will be initialized, thus adding support for multi-threaded
    // tests.
    MULTIPLE_THREADS,
    // No thread pool will be initialized. Useful for tests that want to run
    // single threaded.
    MAIN_THREAD_ONLY,
    DEFAULT = MULTIPLE_THREADS
  };

  // List of traits that are valid inputs for the constructor below.
  struct ValidTrait {
    ValidTrait(MainThreadType);
    ValidTrait(ThreadPoolExecutionMode);
    ValidTrait(NowSource);
    ValidTrait(SubclassCreatesDefaultTaskRunner);
    ValidTrait(ThreadingMode);
  };

  // Constructor accepts zero or more traits which customize the testing
  // environment.
  template <class... ArgTypes,
            class CheckArgumentsAreValid = std::enable_if_t<
                trait_helpers::AreValidTraits<ValidTrait, ArgTypes...>::value>>
  NOINLINE ScopedTaskEnvironment(ArgTypes... args)
      : ScopedTaskEnvironment(
            trait_helpers::GetEnum<MainThreadType, MainThreadType::DEFAULT>(
                args...),
            trait_helpers::GetEnum<ThreadPoolExecutionMode,
                                   ThreadPoolExecutionMode::DEFAULT>(args...),
            trait_helpers::GetEnum<NowSource, NowSource::REAL_TIME>(args...),
            trait_helpers::GetEnum<ThreadingMode, ThreadingMode::DEFAULT>(
                args...),
            trait_helpers::HasTrait<SubclassCreatesDefaultTaskRunner>(args...),
            trait_helpers::NotATraitTag()) {}

  // Waits until no undelayed ThreadPool tasks remain. Then, unregisters the
  // ThreadPoolInstance and the (Thread|Sequenced)TaskRunnerHandle.
  virtual ~ScopedTaskEnvironment();

  // Returns a TaskRunner that schedules tasks on the main thread.
  scoped_refptr<base::SingleThreadTaskRunner> GetMainThreadTaskRunner();

  // Returns whether the main thread's TaskRunner has pending tasks. This will
  // always return true if called right after RunUntilIdle.
  bool MainThreadIsIdle() const;

  // Runs tasks until both the (Thread|Sequenced)TaskRunnerHandle and the
  // ThreadPool's non-delayed queues are empty.
  // While RunUntilIdle() is quite practical and sometimes even necessary -- for
  // example, to flush all tasks bound to Unretained() state before destroying
  // test members -- it should be used with caution per the following warnings:
  //
  // WARNING #1: This may run long (flakily timeout) and even never return! Do
  //             not use this when repeating tasks such as animated web pages
  //             are present.
  // WARNING #2: This may return too early! For example, if used to run until an
  //             incoming event has occurred but that event depends on a task in
  //             a different queue -- e.g. a standalone base::Thread or a system
  //             event.
  //
  // As such, prefer RunLoop::Run() with an explicit RunLoop::QuitClosure() when
  // possible.
  void RunUntilIdle();

  // Only valid for instances with a MOCK_TIME MainThreadType. Fast-forwards
  // virtual time by |delta|, causing all tasks on the main thread with a
  // remaining delay less than or equal to |delta| to be executed before this
  // returns. |delta| must be non-negative.
  // TODO(gab): Make this apply to ThreadPool delayed tasks as well
  // (currently only main thread time is mocked).
  void FastForwardBy(TimeDelta delta);

  // Only valid for instances with a MOCK_TIME MainThreadType.
  // Short for FastForwardBy(TimeDelta::Max()).
  void FastForwardUntilNoTasksRemain();

  // Only valid for instances with a MOCK_TIME MainThreadType. Returns a
  // TickClock whose time is updated by FastForward(By|UntilNoTasksRemain).
  const TickClock* GetMockTickClock() const;
  std::unique_ptr<TickClock> DeprecatedGetMockTickClock();

  // Only valid for instances with a MOCK_TIME MainThreadType. Returns a
  // Clock whose time is updated by FastForward(By|UntilNoTasksRemain). The
  // initial value is implementation defined and should be queried by tests that
  // depend on it.
  // TickClock should be used instead of Clock to measure elapsed time in a
  // process. See time.h.
  const Clock* GetMockClock() const;

  // Only valid for instances with a MOCK_TIME MainThreadType.
  // Returns the current virtual tick time (initially starting at 0).
  base::TimeTicks NowTicks() const;

  // Only valid for instances with a MOCK_TIME MainThreadType.
  // Returns the number of pending tasks (delayed and non-delayed) of the main
  // thread's TaskRunner.
  size_t GetPendingMainThreadTaskCount() const;

  // Only valid for instances with a MOCK_TIME MainThreadType.
  // Returns the delay until the next pending task of the main thread's
  // TaskRunner if there is one, otherwise it returns TimeDelta::Max().
  TimeDelta NextMainThreadPendingTaskDelay() const;

  // Only valid for instances with a MOCK_TIME MainThreadType.
  // Returns true iff the next task is delayed. Returns false if the next task
  // is immediate or if there is no next task.
  bool NextTaskIsDelayed() const;

 protected:
  explicit ScopedTaskEnvironment(ScopedTaskEnvironment&& other);

  constexpr MainThreadType main_thread_type() const {
    return main_thread_type_;
  }

  constexpr ThreadPoolExecutionMode thread_pool_execution_mode() const {
    return thread_pool_execution_mode_;
  }

  // Returns the TimeDomain driving this ScopedTaskEnvironment.
  sequence_manager::TimeDomain* GetTimeDomain() const;

  // For testing the MockTimeDomain.
  void SetAllowTimeToAutoAdvanceUntilForTesting(TimeTicks advance_until);

  sequence_manager::SequenceManager* sequence_manager() const;

  void DeferredInitFromSubclass(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  // Derived classes may need to control when the sequence manager goes away.
  void NotifyDestructionObserversAndReleaseSequenceManager();

 private:
  class MockTimeDomain;
  class TestTaskTracker;

  void InitializeThreadPool();
  void DestroyThreadPool();

  void CompleteInitialization();

  // The template constructor has to be in the header but it delegates to this
  // constructor to initialize all other members out-of-line.
  ScopedTaskEnvironment(MainThreadType main_thread_type,
                        ThreadPoolExecutionMode thread_pool_execution_mode,
                        NowSource now_source,
                        ThreadingMode threading_mode,
                        bool subclass_creates_default_taskrunner,
                        trait_helpers::NotATraitTag tag);

  const MainThreadType main_thread_type_;
  const ThreadPoolExecutionMode thread_pool_execution_mode_;
  const bool subclass_creates_default_taskrunner_;

  std::unique_ptr<sequence_manager::SequenceManager> sequence_manager_;
  std::unique_ptr<MockTimeDomain> mock_time_domain_;

  scoped_refptr<sequence_manager::TaskQueue> task_queue_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  // Only set for instances with a MOCK_TIME MainThreadType.
  std::unique_ptr<Clock> mock_clock_;

#if defined(OS_POSIX) || defined(OS_FUCHSIA)
  // Enables the FileDescriptorWatcher API iff running a MainThreadType::IO.
  std::unique_ptr<FileDescriptorWatcher> file_descriptor_watcher_;
#endif

  const ThreadPoolInstance* thread_pool_ = nullptr;

  // Owned by |thread_pool_|.
  TestTaskTracker* task_tracker_ = nullptr;

  // Ensures destruction of lazy TaskRunners when this is destroyed.
  std::unique_ptr<internal::ScopedLazyTaskRunnerListForTesting>
      scoped_lazy_task_runner_list_for_testing_;

  // Sets RunLoop::Run() to LOG(FATAL) if not Quit() in a timely manner.
  std::unique_ptr<RunLoop::ScopedRunTimeoutForTest> run_loop_timeout_;

  std::unique_ptr<bool> owns_instance_ = std::make_unique<bool>(true);

  DISALLOW_COPY_AND_ASSIGN(ScopedTaskEnvironment);
};

}  // namespace test
}  // namespace base

#endif  // BASE_TEST_SCOPED_TASK_ENVIRONMENT_H_
