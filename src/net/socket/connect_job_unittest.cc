// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/connect_job.h"

#include "base/callback.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/run_loop.h"
#include "base/test/scoped_task_environment.h"
#include "net/base/address_list.h"
#include "net/base/net_errors.h"
#include "net/base/request_priority.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_util.h"
#include "net/socket/connect_job_test_util.h"
#include "net/socket/socket_tag.h"
#include "net/socket/socket_test_util.h"
#include "net/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

class TestConnectJob : public ConnectJob {
 public:
  enum class JobType {
    kSyncSuccess,
    kAsyncSuccess,
    kHung,
  };

  TestConnectJob(JobType job_type,
                 base::TimeDelta timeout_duration,
                 ConnectJob::Delegate* delegate,
                 NetLog* net_log)
      : ConnectJob(
            DEFAULT_PRIORITY,
            timeout_duration,
            CommonConnectJobParams(
                "group_name",
                SocketTag(),
                true /* respect_limits */,
                nullptr /* client_socket_factory */,
                nullptr /* socket_performance_watcher_factory */,
                nullptr /* host_resolver */,
                nullptr /* net_log */,
                nullptr /* websocket_endpoint_lock_manager */),
            delegate,
            NetLogWithSource::Make(net_log,
                                   NetLogSourceType::TRANSPORT_CONNECT_JOB)),
        job_type_(job_type),
        last_seen_priority_(DEFAULT_PRIORITY) {
    switch (job_type_) {
      case JobType::kSyncSuccess:
        socket_data_provider_.set_connect_data(MockConnect(SYNCHRONOUS, OK));
        return;
      case JobType::kAsyncSuccess:
        socket_data_provider_.set_connect_data(MockConnect(ASYNC, OK));
        return;
      case JobType::kHung:
        socket_data_provider_.set_connect_data(
            MockConnect(SYNCHRONOUS, ERR_IO_PENDING));
        return;
    }
  }

  // From ConnectJob:
  LoadState GetLoadState() const override { return LOAD_STATE_IDLE; }
  int ConnectInternal() override {
    SetSocket(std::unique_ptr<StreamSocket>(new MockTCPClientSocket(
        AddressList(), net_log().net_log(), &socket_data_provider_)));
    return socket()->Connect(base::BindOnce(
        &TestConnectJob::NotifyDelegateOfCompletion, base::Unretained(this)));
  }
  void ChangePriorityInternal(RequestPriority priority) override {
    last_seen_priority_ = priority;
  }

  using ConnectJob::ResetTimer;

  // The priority seen during the most recent call to ChangePriorityInternal().
  RequestPriority last_seen_priority() const { return last_seen_priority_; }

 private:
  const JobType job_type_;
  StaticSocketDataProvider socket_data_provider_;
  RequestPriority last_seen_priority_;

  DISALLOW_COPY_AND_ASSIGN(TestConnectJob);
};

class ConnectJobTest : public testing::Test {
 public:
  ConnectJobTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::MOCK_TIME) {}
  ~ConnectJobTest() override = default;

 protected:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  TestConnectJobDelegate delegate_;
};

// Even though a timeout is specified, it doesn't time out on a synchronous
// completion.
TEST_F(ConnectJobTest, NoTimeoutOnSyncCompletion) {
  TestConnectJob job(TestConnectJob::JobType::kSyncSuccess,
                     base::TimeDelta::FromMicroseconds(1), &delegate_,
                     nullptr /* net_log */);
  EXPECT_THAT(job.Connect(), test::IsOk());
}

// Even though a timeout is specified, it doesn't time out on an asynchronous
// completion.
TEST_F(ConnectJobTest, NoTimeoutOnAsyncCompletion) {
  TestConnectJob job(TestConnectJob::JobType::kAsyncSuccess,
                     base::TimeDelta::FromMinutes(1), &delegate_,
                     nullptr /* net_log */);
  ASSERT_THAT(job.Connect(), test::IsError(ERR_IO_PENDING));
  EXPECT_THAT(delegate_.WaitForResult(), test::IsOk());
}

// Job shouldn't timeout when passed a TimeDelta of zero.
TEST_F(ConnectJobTest, NoTimeoutWithNoTimeDelta) {
  TestConnectJob job(TestConnectJob::JobType::kHung, base::TimeDelta(),
                     &delegate_, nullptr /* net_log */);
  ASSERT_THAT(job.Connect(), test::IsError(ERR_IO_PENDING));
  scoped_task_environment_.RunUntilIdle();
  EXPECT_FALSE(delegate_.has_result());
}

// Make sure that ChangePriority() works, and new priority is visible to
// subclasses during the SetPriorityInternal call.
TEST_F(ConnectJobTest, SetPriority) {
  TestConnectJob job(TestConnectJob::JobType::kAsyncSuccess,
                     base::TimeDelta::FromMicroseconds(1), &delegate_,
                     nullptr /* net_log */);
  ASSERT_THAT(job.Connect(), test::IsError(ERR_IO_PENDING));

  job.ChangePriority(HIGHEST);
  EXPECT_EQ(HIGHEST, job.priority());
  EXPECT_EQ(HIGHEST, job.last_seen_priority());

  job.ChangePriority(MEDIUM);
  EXPECT_EQ(MEDIUM, job.priority());
  EXPECT_EQ(MEDIUM, job.last_seen_priority());

  EXPECT_THAT(delegate_.WaitForResult(), test::IsOk());
}

TEST_F(ConnectJobTest, TimedOut) {
  const base::TimeDelta kTimeout = base::TimeDelta::FromHours(1);
  TestNetLog log;

  std::unique_ptr<TestConnectJob> job = std::make_unique<TestConnectJob>(
      TestConnectJob::JobType::kHung, kTimeout, &delegate_, &log);
  ASSERT_THAT(job->Connect(), test::IsError(ERR_IO_PENDING));

  // Nothing should happen before the specified time.
  scoped_task_environment_.FastForwardBy(kTimeout -
                                         base::TimeDelta::FromMilliseconds(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(delegate_.has_result());

  // At which point the job should time out.
  scoped_task_environment_.FastForwardBy(base::TimeDelta::FromMilliseconds(1));
  EXPECT_THAT(delegate_.WaitForResult(), test::IsError(ERR_TIMED_OUT));

  // Have to delete the job for it to log the end event.
  job.reset();

  TestNetLogEntry::List entries;
  log.GetEntries(&entries);

  EXPECT_EQ(6u, entries.size());
  EXPECT_TRUE(LogContainsBeginEvent(entries, 0,
                                    NetLogEventType::SOCKET_POOL_CONNECT_JOB));
  EXPECT_TRUE(LogContainsBeginEvent(
      entries, 1, NetLogEventType::SOCKET_POOL_CONNECT_JOB_CONNECT));
  EXPECT_TRUE(LogContainsEvent(entries, 2,
                               NetLogEventType::CONNECT_JOB_SET_SOCKET,
                               NetLogEventPhase::NONE));
  EXPECT_TRUE(LogContainsEvent(
      entries, 3, NetLogEventType::SOCKET_POOL_CONNECT_JOB_TIMED_OUT,
      NetLogEventPhase::NONE));
  EXPECT_TRUE(LogContainsEndEvent(
      entries, 4, NetLogEventType::SOCKET_POOL_CONNECT_JOB_CONNECT));
  EXPECT_TRUE(LogContainsEndEvent(entries, 5,
                                  NetLogEventType::SOCKET_POOL_CONNECT_JOB));
}

TEST_F(ConnectJobTest, TimedOutWithRestartedTimer) {
  const base::TimeDelta kTimeout = base::TimeDelta::FromHours(1);

  TestConnectJob job(TestConnectJob::JobType::kHung, kTimeout, &delegate_,
                     nullptr /* net_log */);
  ASSERT_THAT(job.Connect(), test::IsError(ERR_IO_PENDING));

  // Nothing should happen before the specified time.
  scoped_task_environment_.FastForwardBy(kTimeout -
                                         base::TimeDelta::FromMilliseconds(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(delegate_.has_result());

  // Make sure restarting the timer is respected.
  job.ResetTimer(kTimeout);
  scoped_task_environment_.FastForwardBy(kTimeout -
                                         base::TimeDelta::FromMilliseconds(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(delegate_.has_result());

  scoped_task_environment_.FastForwardBy(base::TimeDelta::FromMilliseconds(1));
  EXPECT_THAT(delegate_.WaitForResult(), test::IsError(ERR_TIMED_OUT));
}

}  // namespace
}  // namespace net
