// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include "base/atomicops.h"
#include "base/bind.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/atomic_flag.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_test.h"

namespace base {

namespace {

// A thread that waits for the caller to signal an event before proceeding to
// call Action::Run().
class PostingThread {
 public:
  class Action {
   public:
    virtual ~Action() = default;

    // Called after the thread is started and |start_event_| is signalled.
    virtual void Run() = 0;

   protected:
    Action() = default;

   private:
    DISALLOW_COPY_AND_ASSIGN(Action);
  };

  // Creates a PostingThread where the thread waits on |start_event| before
  // calling action->Run(). If a thread is returned, the thread is guaranteed to
  // be allocated and running and the caller must call Join() before destroying
  // the PostingThread.
  static std::unique_ptr<PostingThread> Create(WaitableEvent* start_event,
                                               std::unique_ptr<Action> action) {
    auto posting_thread =
        WrapUnique(new PostingThread(start_event, std::move(action)));

    if (!posting_thread->Start())
      return nullptr;

    return posting_thread;
  }

  ~PostingThread() { DCHECK_EQ(!thread_handle_.is_null(), join_called_); }

  void Join() {
    PlatformThread::Join(thread_handle_);
    join_called_ = true;
  }

 private:
  class Delegate final : public PlatformThread::Delegate {
   public:
    Delegate(PostingThread* outer, std::unique_ptr<Action> action)
        : outer_(outer), action_(std::move(action)) {
      DCHECK(outer_);
      DCHECK(action_);
    }

    ~Delegate() override = default;

   private:
    void ThreadMain() override {
      outer_->thread_started_.Signal();
      outer_->start_event_->Wait();
      action_->Run();
    }

    PostingThread* const outer_;
    const std::unique_ptr<Action> action_;

    DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  PostingThread(WaitableEvent* start_event, std::unique_ptr<Action> delegate)
      : start_event_(start_event),
        thread_started_(WaitableEvent::ResetPolicy::MANUAL,
                        WaitableEvent::InitialState::NOT_SIGNALED),
        delegate_(this, std::move(delegate)) {
    DCHECK(start_event_);
  }

  bool Start() {
    bool thread_created =
        PlatformThread::Create(0, &delegate_, &thread_handle_);
    if (thread_created)
      thread_started_.Wait();

    return thread_created;
  }

  bool join_called_ = false;
  WaitableEvent* const start_event_;
  WaitableEvent thread_started_;
  Delegate delegate_;

  PlatformThreadHandle thread_handle_;

  DISALLOW_COPY_AND_ASSIGN(PostingThread);
};

class MessageLoopPerfTest : public ::testing::TestWithParam<int> {
 public:
  MessageLoopPerfTest()
      : message_loop_task_runner_(SequencedTaskRunnerHandle::Get()),
        run_posting_threads_(WaitableEvent::ResetPolicy::MANUAL,
                             WaitableEvent::InitialState::NOT_SIGNALED) {}

  static std::string ParamInfoToString(
      ::testing::TestParamInfo<int> param_info) {
    return PostingThreadCountToString(param_info.param);
  }

  static std::string PostingThreadCountToString(int posting_threads) {
    // Special case 1 thread for thread vs threads.
    if (posting_threads == 1)
      return "1_Posting_Thread";

    return StringPrintf("%d_Posting_Threads", posting_threads);
  }

 protected:
  class ContinuouslyPostTasks final : public PostingThread::Action {
   public:
    ContinuouslyPostTasks(MessageLoopPerfTest* outer) : outer_(outer) {
      DCHECK(outer_);
    }
    ~ContinuouslyPostTasks() override = default;

   private:
    void Run() override {
      RepeatingClosure task_to_run =
          BindRepeating([](size_t* num_tasks_run) { ++*num_tasks_run; },
                        &outer_->num_tasks_run_);
      while (!outer_->stop_posting_threads_.IsSet()) {
        outer_->message_loop_task_runner_->PostTask(FROM_HERE, task_to_run);
        subtle::NoBarrier_AtomicIncrement(&outer_->num_tasks_posted_, 1);
      }
    }

    MessageLoopPerfTest* const outer_;

    DISALLOW_COPY_AND_ASSIGN(ContinuouslyPostTasks);
  };

  void SetUp() override {
    // This check is here because we can't ASSERT_TRUE in the constructor.
    ASSERT_TRUE(message_loop_task_runner_);
  }

  // Runs ActionType::Run() on |num_posting_threads| and requests test
  // termination around |duration|.
  template <typename ActionType>
  void RunTest(const int num_posting_threads, TimeDelta duration) {
    std::vector<std::unique_ptr<PostingThread>> threads;
    for (int i = 0; i < num_posting_threads; ++i) {
      threads.emplace_back(PostingThread::Create(
          &run_posting_threads_, std::make_unique<ActionType>(this)));
      // Don't assert here to simplify the code that requires a Join() call for
      // every created PostingThread.
      EXPECT_TRUE(threads[i]);
    }

    RunLoop run_loop;
    message_loop_task_runner_->PostDelayedTask(
        FROM_HERE,
        BindOnce(
            [](RunLoop* run_loop, AtomicFlag* stop_posting_threads) {
              stop_posting_threads->Set();
              run_loop->Quit();
            },
            &run_loop, &stop_posting_threads_),
        duration);

    TimeTicks post_task_start = TimeTicks::Now();
    run_posting_threads_.Signal();

    TimeTicks run_loop_start = TimeTicks::Now();
    run_loop.Run();
    tasks_run_duration_ = TimeTicks::Now() - run_loop_start;

    for (auto& thread : threads)
      thread->Join();

    tasks_posted_duration_ = TimeTicks::Now() - post_task_start;
  }

  size_t num_tasks_posted() const {
    return subtle::NoBarrier_Load(&num_tasks_posted_);
  }

  TimeDelta tasks_posted_duration() const { return tasks_posted_duration_; }

  size_t num_tasks_run() const { return num_tasks_run_; }

  TimeDelta tasks_run_duration() const { return tasks_run_duration_; }

 private:
  MessageLoop message_loop_;

  // Accessed on multiple threads, thread-safe or constant:
  const scoped_refptr<SequencedTaskRunner> message_loop_task_runner_;
  WaitableEvent run_posting_threads_;
  AtomicFlag stop_posting_threads_;
  subtle::AtomicWord num_tasks_posted_ = 0;

  // Accessed only on the test case thread:
  TimeDelta tasks_posted_duration_;
  TimeDelta tasks_run_duration_;
  size_t num_tasks_run_ = 0;

  DISALLOW_COPY_AND_ASSIGN(MessageLoopPerfTest);
};

}  // namespace

TEST_P(MessageLoopPerfTest, PostTaskRate) {
  // Measures the average rate of posting tasks from different threads and the
  // average rate that the message loop is running those tasks.
  RunTest<ContinuouslyPostTasks>(GetParam(), TimeDelta::FromSeconds(3));
  perf_test::PrintResult("task_posting", "",
                         PostingThreadCountToString(GetParam()),
                         tasks_posted_duration().InMicroseconds() /
                             static_cast<double>(num_tasks_posted()),
                         "us/task", true);
  perf_test::PrintResult("task_running", "",
                         PostingThreadCountToString(GetParam()),
                         tasks_run_duration().InMicroseconds() /
                             static_cast<double>(num_tasks_run()),
                         "us/task", true);
}

INSTANTIATE_TEST_CASE_P(,
                        MessageLoopPerfTest,
                        ::testing::Values(1, 5, 10),
                        MessageLoopPerfTest::ParamInfoToString);
}  // namespace base
