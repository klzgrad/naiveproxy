// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop/message_loop_task_runner.h"

#include <string>
#include <utility>

#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/debug/task_annotator.h"
#include "base/macros.h"
#include "base/memory/scoped_refptr.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_loop_task_runner.h"
#include "base/message_loop/message_pump.h"
#include "base/message_loop/sequenced_task_source.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_test.h"

namespace base {

namespace {

// Tests below will post tasks in a loop until |kPostTaskPerfTestDuration| has
// elapsed.
constexpr TimeDelta kPostTaskPerfTestDuration =
    base::TimeDelta::FromSeconds(30);

}  // namespace

class FakeObserver : public SequencedTaskSource::Observer {
 public:
  // SequencedTaskSource::Observer
  void WillQueueTask(PendingTask* task) override {}
  void DidQueueTask(bool was_empty) override {}

  virtual void RunTask(PendingTask* task) { std::move(task->task).Run(); }
};

// Exercises MessageLoopTaskRunner's multi-threaded queue in isolation.
class BasicPostTaskPerfTest : public testing::Test {
 public:
  void Run(int batch_size,
           int tasks_per_reload,
           std::unique_ptr<FakeObserver> task_source_observer) {
    base::TimeTicks start = base::TimeTicks::Now();
    base::TimeTicks now;
    FakeObserver* task_source_observer_raw = task_source_observer.get();
    auto message_loop_task_runner =
        MakeRefCounted<internal::MessageLoopTaskRunner>(
            std::move(task_source_observer));
    uint32_t num_posted = 0;
    do {
      for (int i = 0; i < batch_size; ++i) {
        for (int j = 0; j < tasks_per_reload; ++j) {
          message_loop_task_runner->PostTask(FROM_HERE, DoNothing());
          num_posted++;
        }
        // The outgoing queue will only be reloaded when first entering this
        // loop.
        while (message_loop_task_runner->HasTasks()) {
          auto task = message_loop_task_runner->TakeTask();
          task_source_observer_raw->RunTask(&task);
        }
      }

      now = base::TimeTicks::Now();
    } while (now - start < kPostTaskPerfTestDuration);
    std::string trace = StringPrintf("%d_tasks_per_reload", tasks_per_reload);
    perf_test::PrintResult(
        "task", "", trace,
        (now - start).InMicroseconds() / static_cast<double>(num_posted),
        "us/task", true);
  }
};

TEST_F(BasicPostTaskPerfTest, OneTaskPerReload) {
  Run(10000, 1, std::make_unique<FakeObserver>());
}

TEST_F(BasicPostTaskPerfTest, TenTasksPerReload) {
  Run(10000, 10, std::make_unique<FakeObserver>());
}

TEST_F(BasicPostTaskPerfTest, OneHundredTasksPerReload) {
  Run(1000, 100, std::make_unique<FakeObserver>());
}

class StubMessagePump : public MessagePump {
 public:
  StubMessagePump() = default;
  ~StubMessagePump() override = default;

  // MessagePump:
  void Run(Delegate* delegate) override {}
  void Quit() override {}
  void ScheduleWork() override {}
  void ScheduleDelayedWork(const TimeTicks& delayed_work_time) override {}
};

// Simulates the overhead of hooking TaskAnnotator and ScheduleWork() to the
// post task machinery.
class FakeObserverSimulatingOverhead : public FakeObserver {
 public:
  FakeObserverSimulatingOverhead() = default;

  // FakeObserver:
  void WillQueueTask(PendingTask* task) final {
    task_annotator_.WillQueueTask("MessageLoop::PostTask", task);
  }

  void DidQueueTask(bool was_empty) final {
    AutoLock scoped_lock(message_loop_lock_);
    pump_->ScheduleWork();
  }

  void RunTask(PendingTask* task) final {
    task_annotator_.RunTask("MessageLoop::PostTask", task);
  }

 private:
  // Simulates overhead from ScheduleWork() and TaskAnnotator calls involved in
  // a real PostTask (stores the StubMessagePump in a pointer to force a virtual
  // dispatch for ScheduleWork() and be closer to reality).
  Lock message_loop_lock_;
  std::unique_ptr<MessagePump> pump_{std::make_unique<StubMessagePump>()};
  debug::TaskAnnotator task_annotator_;

  DISALLOW_COPY_AND_ASSIGN(FakeObserverSimulatingOverhead);
};

TEST_F(BasicPostTaskPerfTest, OneTaskPerReloadWithOverhead) {
  Run(10000, 1, std::make_unique<FakeObserverSimulatingOverhead>());
}

TEST_F(BasicPostTaskPerfTest, TenTasksPerReloadWithOverhead) {
  Run(10000, 10, std::make_unique<FakeObserverSimulatingOverhead>());
}

TEST_F(BasicPostTaskPerfTest, OneHundredTasksPerReloadWithOverhead) {
  Run(1000, 100, std::make_unique<FakeObserverSimulatingOverhead>());
}

// Exercises the full MessageLoop/RunLoop machinery.
class IntegratedPostTaskPerfTest : public testing::Test {
 public:
  void Run(int batch_size, int tasks_per_reload) {
    base::TimeTicks start = base::TimeTicks::Now();
    base::TimeTicks now;
    MessageLoop loop;
    uint32_t num_posted = 0;
    do {
      for (int i = 0; i < batch_size; ++i) {
        for (int j = 0; j < tasks_per_reload; ++j) {
          loop->task_runner()->PostTask(FROM_HERE, DoNothing());
          num_posted++;
        }
        RunLoop().RunUntilIdle();
      }

      now = base::TimeTicks::Now();
    } while (now - start < kPostTaskPerfTestDuration);
    std::string trace = StringPrintf("%d_tasks_per_reload", tasks_per_reload);
    perf_test::PrintResult(
        "task", "", trace,
        (now - start).InMicroseconds() / static_cast<double>(num_posted),
        "us/task", true);
  }
};

TEST_F(IntegratedPostTaskPerfTest, OneTaskPerReload) {
  Run(10000, 1);
}

TEST_F(IntegratedPostTaskPerfTest, TenTasksPerReload) {
  Run(10000, 10);
}

TEST_F(IntegratedPostTaskPerfTest, OneHundredTasksPerReload) {
  Run(1000, 100);
}

}  // namespace base
