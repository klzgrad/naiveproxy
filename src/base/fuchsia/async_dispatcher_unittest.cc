// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/async_dispatcher.h"

#include <lib/async/default.h>
#include <lib/async/task.h>
#include <lib/async/wait.h>
#include <lib/zx/job.h>
#include <lib/zx/socket.h>

#include "base/callback.h"
#include "base/process/launch.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

namespace base {

namespace {

struct TestTask : public async_task_t {
  explicit TestTask() {
    state = ASYNC_STATE_INIT;
    handler = &TaskProc;
    deadline = 0;
  }

  static void TaskProc(async_dispatcher_t* async,
                       async_task_t* task,
                       zx_status_t status);

  int num_calls = 0;
  int repeats = 1;
  OnceClosure on_call;
  zx_status_t last_status = ZX_OK;
};

// static
void TestTask::TaskProc(async_dispatcher_t* async,
                        async_task_t* task,
                        zx_status_t status) {
  EXPECT_EQ(async, async_get_default_dispatcher());
  EXPECT_TRUE(status == ZX_OK || status == ZX_ERR_CANCELED)
      << "status: " << status;

  auto* test_task = static_cast<TestTask*>(task);
  test_task->num_calls++;
  test_task->last_status = status;

  if (test_task->on_call)
    std::move(test_task->on_call).Run();

  if (test_task->num_calls < test_task->repeats)
    async_post_task(async, task);
};

struct TestWait : public async_wait_t {
  TestWait(zx_handle_t handle,
           zx_signals_t signals) {
    state = ASYNC_STATE_INIT;
    handler = &HandleProc;
    object = handle;
    trigger = signals;
  }

  static void HandleProc(async_dispatcher_t* async,
                         async_wait_t* wait,
                         zx_status_t status,
                         const zx_packet_signal_t* signal);
  int num_calls = 0;
  OnceClosure on_call;
  zx_status_t last_status = ZX_OK;
};

// static
void TestWait::HandleProc(async_dispatcher_t* async,
                          async_wait_t* wait,
                          zx_status_t status,
                          const zx_packet_signal_t* signal) {
  EXPECT_EQ(async, async_get_default_dispatcher());
  EXPECT_TRUE(status == ZX_OK || status == ZX_ERR_CANCELED)
      << "status: " << status;

  auto* test_wait = static_cast<TestWait*>(wait);

  test_wait->num_calls++;
  test_wait->last_status = status;

  if (test_wait->on_call)
    std::move(test_wait->on_call).Run();
}

struct TestException : public async_exception_t {
  TestException(zx_handle_t handle) {
    state = ASYNC_STATE_INIT;
    handler = &HandleProc;
    task = handle;
    options = 0;
  }

  static void HandleProc(async_dispatcher_t* async,
                         async_exception_t* wait,
                         zx_status_t status,
                         const zx_port_packet_t* packet);
  int num_calls = 0;
  OnceClosure on_call;
  zx_status_t last_status = ZX_OK;
};

// static
void TestException::HandleProc(async_dispatcher_t* async,
                               async_exception_t* wait,
                               zx_status_t status,
                               const zx_port_packet_t* packet) {
  EXPECT_EQ(async, async_get_default_dispatcher());

  auto* test_wait = static_cast<TestException*>(wait);

  test_wait->num_calls++;
  test_wait->last_status = status;

  if (test_wait->on_call)
    std::move(test_wait->on_call).Run();
}

}  // namespace

class AsyncDispatcherTest : public MultiProcessTest {
 public:
  AsyncDispatcherTest() {
    dispatcher_ = std::make_unique<AsyncDispatcher>();

    async_ = async_get_default_dispatcher();
    EXPECT_TRUE(async_);

    EXPECT_EQ(zx::socket::create(ZX_SOCKET_DATAGRAM, &socket1_, &socket2_),
              ZX_OK);
  }

  ~AsyncDispatcherTest() override = default;

  void RunUntilIdle() {
    while (true) {
      zx_status_t status = dispatcher_->DispatchOrWaitUntil(0);
      if (status != ZX_OK) {
        EXPECT_EQ(status, ZX_ERR_TIMED_OUT);
        break;
      }
    }
  }

 protected:
  std::unique_ptr<AsyncDispatcher> dispatcher_;

  async_dispatcher_t* async_ = nullptr;

  zx::socket socket1_;
  zx::socket socket2_;
};

TEST_F(AsyncDispatcherTest, PostTask) {
  TestTask task;
  ASSERT_EQ(async_post_task(async_, &task), ZX_OK);
  dispatcher_->DispatchOrWaitUntil(0);
  EXPECT_EQ(task.num_calls, 1);
  EXPECT_EQ(task.last_status, ZX_OK);
}

TEST_F(AsyncDispatcherTest, TaskRepeat) {
  TestTask task;
  task.repeats = 2;
  ASSERT_EQ(async_post_task(async_, &task), ZX_OK);
  RunUntilIdle();
  EXPECT_EQ(task.num_calls, 2);
  EXPECT_EQ(task.last_status, ZX_OK);
}

TEST_F(AsyncDispatcherTest, DelayedTask) {
  TestTask task;
  constexpr auto kDelay = TimeDelta::FromMilliseconds(5);
  TimeTicks started = TimeTicks::Now();
  task.deadline = zx_deadline_after(kDelay.InNanoseconds());
  ASSERT_EQ(async_post_task(async_, &task), ZX_OK);
  zx_status_t status = dispatcher_->DispatchOrWaitUntil(zx_deadline_after(
      (kDelay + TestTimeouts::tiny_timeout()).InNanoseconds()));
  EXPECT_EQ(status, ZX_OK);
  EXPECT_GE(TimeTicks::Now() - started, kDelay);
}

TEST_F(AsyncDispatcherTest, CancelTask) {
  TestTask task;
  ASSERT_EQ(async_post_task(async_, &task), ZX_OK);
  ASSERT_EQ(async_cancel_task(async_, &task), ZX_OK);
  RunUntilIdle();
  EXPECT_EQ(task.num_calls, 0);
}

TEST_F(AsyncDispatcherTest, TaskObserveShutdown) {
  TestTask task;
  ASSERT_EQ(async_post_task(async_, &task), ZX_OK);
  dispatcher_.reset();

  EXPECT_EQ(task.num_calls, 1);
  EXPECT_EQ(task.last_status, ZX_ERR_CANCELED);
}

TEST_F(AsyncDispatcherTest, Wait) {
  TestWait wait(socket1_.get(), ZX_SOCKET_READABLE);
  EXPECT_EQ(async_begin_wait(async_, &wait), ZX_OK);

  // Handler shouldn't be called because the event wasn't signaled.
  RunUntilIdle();
  EXPECT_EQ(wait.num_calls, 0);

  char byte = 0;
  EXPECT_EQ(socket2_.write(/*options=*/0, &byte, sizeof(byte),
                           /*actual=*/nullptr),
            ZX_OK);

  zx_status_t status = dispatcher_->DispatchOrWaitUntil(
      zx_deadline_after(TestTimeouts::tiny_timeout().InNanoseconds()));
  EXPECT_EQ(status, ZX_OK);

  EXPECT_EQ(wait.num_calls, 1);
  EXPECT_EQ(wait.last_status, ZX_OK);
}

TEST_F(AsyncDispatcherTest, CancelWait) {
  TestWait wait(socket1_.get(), ZX_SOCKET_READABLE);
  EXPECT_EQ(async_begin_wait(async_, &wait), ZX_OK);

  char byte = 0;
  EXPECT_EQ(socket2_.write(/*options=*/0, &byte, sizeof(byte),
                           /*actual=*/nullptr),
            ZX_OK);

  EXPECT_EQ(async_cancel_wait(async_, &wait), ZX_OK);

  RunUntilIdle();
  EXPECT_EQ(wait.num_calls, 0);
}

TEST_F(AsyncDispatcherTest, WaitShutdown) {
  TestWait wait(socket1_.get(), ZX_SOCKET_READABLE);
  EXPECT_EQ(async_begin_wait(async_, &wait), ZX_OK);
  RunUntilIdle();
  dispatcher_.reset();

  EXPECT_EQ(wait.num_calls, 1);
  EXPECT_EQ(wait.last_status, ZX_ERR_CANCELED);
}

// Sub-process which crashes itself, to generate an exception-port event.
MULTIPROCESS_TEST_MAIN(AsyncDispatcherCrashingChild) {
  IMMEDIATE_CRASH();
  return 0;
}

TEST_F(AsyncDispatcherTest, BindExceptionPort) {
  zx::job child_job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &child_job), ZX_OK);

  // Bind |child_job|'s exception port to the dispatcher.
  TestException exception(child_job.get());
  EXPECT_EQ(async_bind_exception_port(async_, &exception), ZX_OK);

  // Launch a child process in the job, that will immediately crash.
  LaunchOptions options;
  options.job_handle = child_job.get();
  Process child =
      SpawnChildWithOptions("AsyncDispatcherCrashingChild", options);
  ASSERT_TRUE(child.IsValid());

  // Wait for the exception event to be handled.
  EXPECT_EQ(
      dispatcher_->DispatchOrWaitUntil(
          (TimeTicks::Now() + TestTimeouts::action_max_timeout()).ToZxTime()),
      ZX_OK);
  EXPECT_EQ(exception.num_calls, 1);
  EXPECT_EQ(exception.last_status, ZX_OK);

  EXPECT_EQ(async_unbind_exception_port(async_, &exception), ZX_OK);
  ASSERT_EQ(child_job.kill(), ZX_OK);
}

TEST_F(AsyncDispatcherTest, CancelExceptionPort) {
  zx::job child_job;
  ASSERT_EQ(zx::job::create(*zx::job::default_job(), 0, &child_job), ZX_OK);

  // Bind |child_job|'s exception port to the dispatcher.
  TestException exception(child_job.get());
  EXPECT_EQ(async_bind_exception_port(async_, &exception), ZX_OK);

  // Tear-down the dispatcher, and verify that the |exception| is cancelled.
  dispatcher_ = nullptr;
  EXPECT_EQ(exception.num_calls, 1);
  EXPECT_EQ(exception.last_status, ZX_ERR_CANCELED);

  ASSERT_EQ(child_job.kill(), ZX_OK);
}

}  // namespace base
