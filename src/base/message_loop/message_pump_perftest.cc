// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/format_macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/message_loop/message_loop_current.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/synchronization/waitable_event.h"
#include "base/task/sequence_manager/sequence_manager_impl.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/perf/perf_test.h"

#if defined(OS_ANDROID)
#include "base/android/java_handler_thread.h"
#endif

namespace base {

class ScheduleWorkTest : public testing::Test {
 public:
  ScheduleWorkTest() : counter_(0) {}

  void SetUp() override {
    if (base::ThreadTicks::IsSupported())
      base::ThreadTicks::WaitUntilInitialized();
  }

  void Increment(uint64_t amount) { counter_ += amount; }

  void Schedule(int index) {
    base::TimeTicks start = base::TimeTicks::Now();
    base::ThreadTicks thread_start;
    if (ThreadTicks::IsSupported())
      thread_start = base::ThreadTicks::Now();
    base::TimeDelta minimum = base::TimeDelta::Max();
    base::TimeDelta maximum = base::TimeDelta();
    base::TimeTicks now, lastnow = start;
    uint64_t schedule_calls = 0u;
    do {
      for (size_t i = 0; i < kBatchSize; ++i) {
        target_message_loop_base()->GetMessagePump()->ScheduleWork();
        schedule_calls++;
      }
      now = base::TimeTicks::Now();
      base::TimeDelta laptime = now - lastnow;
      lastnow = now;
      minimum = std::min(minimum, laptime);
      maximum = std::max(maximum, laptime);
    } while (now - start < base::TimeDelta::FromSeconds(kTargetTimeSec));

    scheduling_times_[index] = now - start;
    if (ThreadTicks::IsSupported())
      scheduling_thread_times_[index] =
          base::ThreadTicks::Now() - thread_start;
    min_batch_times_[index] = minimum;
    max_batch_times_[index] = maximum;
    target_message_loop_base()->GetTaskRunner()->PostTask(
        FROM_HERE, base::BindOnce(&ScheduleWorkTest::Increment,
                                  base::Unretained(this), schedule_calls));
  }

  void ScheduleWork(MessageLoop::Type target_type, int num_scheduling_threads) {
#if defined(OS_ANDROID)
    if (target_type == MessageLoop::TYPE_JAVA) {
      java_thread_.reset(new android::JavaHandlerThread("target"));
      java_thread_->Start();
    } else
#endif
    {
      target_.reset(new Thread("test"));

      Thread::Options options(target_type, 0u);

      std::unique_ptr<MessageLoop> message_loop =
          MessageLoop::CreateUnbound(target_type);
      message_loop_ = message_loop.get();
      options.task_environment =
          new internal::MessageLoopTaskEnvironment(std::move(message_loop));
      target_->StartWithOptions(options);

      // Without this, it's possible for the scheduling threads to start and run
      // before the target thread. In this case, the scheduling threads will
      // call target_message_loop()->ScheduleWork(), which dereferences the
      // loop's message pump, which is only created after the target thread has
      // finished starting.
      target_->WaitUntilThreadStarted();
    }

    std::vector<std::unique_ptr<Thread>> scheduling_threads;
    scheduling_times_.reset(new base::TimeDelta[num_scheduling_threads]);
    scheduling_thread_times_.reset(new base::TimeDelta[num_scheduling_threads]);
    min_batch_times_.reset(new base::TimeDelta[num_scheduling_threads]);
    max_batch_times_.reset(new base::TimeDelta[num_scheduling_threads]);

    for (int i = 0; i < num_scheduling_threads; ++i) {
      scheduling_threads.push_back(std::make_unique<Thread>("posting thread"));
      scheduling_threads[i]->Start();
    }

    for (int i = 0; i < num_scheduling_threads; ++i) {
      scheduling_threads[i]->task_runner()->PostTask(
          FROM_HERE, base::BindOnce(&ScheduleWorkTest::Schedule,
                                    base::Unretained(this), i));
    }

    for (int i = 0; i < num_scheduling_threads; ++i) {
      scheduling_threads[i]->Stop();
    }
#if defined(OS_ANDROID)
    if (target_type == MessageLoop::TYPE_JAVA) {
      java_thread_->Stop();
      java_thread_.reset();
    } else
#endif
    {
      target_->Stop();
      target_.reset();
    }
    base::TimeDelta total_time;
    base::TimeDelta total_thread_time;
    base::TimeDelta min_batch_time = base::TimeDelta::Max();
    base::TimeDelta max_batch_time = base::TimeDelta();
    for (int i = 0; i < num_scheduling_threads; ++i) {
      total_time += scheduling_times_[i];
      total_thread_time += scheduling_thread_times_[i];
      min_batch_time = std::min(min_batch_time, min_batch_times_[i]);
      max_batch_time = std::max(max_batch_time, max_batch_times_[i]);
    }
    std::string trace = StringPrintf(
        "%d_threads_scheduling_to_%s_pump",
        num_scheduling_threads,
        target_type == MessageLoop::TYPE_IO
            ? "io"
            : (target_type == MessageLoop::TYPE_UI ? "ui" : "default"));
    perf_test::PrintResult(
        "task",
        "",
        trace,
        total_time.InMicroseconds() / static_cast<double>(counter_),
        "us/task",
        true);
    perf_test::PrintResult(
        "task",
        "_min_batch_time",
        trace,
        min_batch_time.InMicroseconds() / static_cast<double>(kBatchSize),
        "us/task",
        false);
    perf_test::PrintResult(
        "task",
        "_max_batch_time",
        trace,
        max_batch_time.InMicroseconds() / static_cast<double>(kBatchSize),
        "us/task",
        false);
    if (ThreadTicks::IsSupported()) {
      perf_test::PrintResult(
          "task",
          "_thread_time",
          trace,
          total_thread_time.InMicroseconds() / static_cast<double>(counter_),
          "us/task",
          true);
    }
  }

  sequence_manager::internal::SequenceManagerImpl* target_message_loop_base() {
#if defined(OS_ANDROID)
    if (java_thread_)
      return java_thread_->message_loop()->GetSequenceManagerImpl();
#endif
    return MessageLoopCurrent::Get()->GetCurrentSequenceManagerImpl();
  }

 private:
  std::unique_ptr<Thread> target_;
  MessageLoop* message_loop_;
#if defined(OS_ANDROID)
  std::unique_ptr<android::JavaHandlerThread> java_thread_;
#endif
  std::unique_ptr<base::TimeDelta[]> scheduling_times_;
  std::unique_ptr<base::TimeDelta[]> scheduling_thread_times_;
  std::unique_ptr<base::TimeDelta[]> min_batch_times_;
  std::unique_ptr<base::TimeDelta[]> max_batch_times_;
  uint64_t counter_;

  static const size_t kTargetTimeSec = 5;
  static const size_t kBatchSize = 1000;
};

TEST_F(ScheduleWorkTest, ThreadTimeToIOFromOneThread) {
  ScheduleWork(MessageLoop::TYPE_IO, 1);
}

TEST_F(ScheduleWorkTest, ThreadTimeToIOFromTwoThreads) {
  ScheduleWork(MessageLoop::TYPE_IO, 2);
}

TEST_F(ScheduleWorkTest, ThreadTimeToIOFromFourThreads) {
  ScheduleWork(MessageLoop::TYPE_IO, 4);
}

TEST_F(ScheduleWorkTest, ThreadTimeToUIFromOneThread) {
  ScheduleWork(MessageLoop::TYPE_UI, 1);
}

TEST_F(ScheduleWorkTest, ThreadTimeToUIFromTwoThreads) {
  ScheduleWork(MessageLoop::TYPE_UI, 2);
}

TEST_F(ScheduleWorkTest, ThreadTimeToUIFromFourThreads) {
  ScheduleWork(MessageLoop::TYPE_UI, 4);
}

TEST_F(ScheduleWorkTest, ThreadTimeToDefaultFromOneThread) {
  ScheduleWork(MessageLoop::TYPE_DEFAULT, 1);
}

TEST_F(ScheduleWorkTest, ThreadTimeToDefaultFromTwoThreads) {
  ScheduleWork(MessageLoop::TYPE_DEFAULT, 2);
}

TEST_F(ScheduleWorkTest, ThreadTimeToDefaultFromFourThreads) {
  ScheduleWork(MessageLoop::TYPE_DEFAULT, 4);
}

#if defined(OS_ANDROID)
TEST_F(ScheduleWorkTest, ThreadTimeToJavaFromOneThread) {
  ScheduleWork(MessageLoop::TYPE_JAVA, 1);
}

TEST_F(ScheduleWorkTest, ThreadTimeToJavaFromTwoThreads) {
  ScheduleWork(MessageLoop::TYPE_JAVA, 2);
}

TEST_F(ScheduleWorkTest, ThreadTimeToJavaFromFourThreads) {
  ScheduleWork(MessageLoop::TYPE_JAVA, 4);
}
#endif

}  // namespace base
