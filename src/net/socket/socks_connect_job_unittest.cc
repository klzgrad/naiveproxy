// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socks_client_socket_pool.h"

#include "base/callback.h"
#include "base/containers/span.h"
#include "base/run_loop.h"
#include "base/test/scoped_task_environment.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "net/base/load_timing_info.h"
#include "net/base/load_timing_info_test_util.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/log/net_log.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/connect_job_test_util.h"
#include "net/socket/socket_tag.h"
#include "net/socket/socket_test_util.h"
#include "net/socket/socks_connect_job.h"
#include "net/socket/transport_client_socket_pool.h"
#include "net/socket/transport_connect_job.h"
#include "net/test/gtest_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

const char kProxyHostName[] = "proxy.test";
const int kProxyPort = 4321;

constexpr base::TimeDelta kTinyTime = base::TimeDelta::FromMicroseconds(1);

class SOCKSConnectJobTest : public testing::Test,
                            public WithScopedTaskEnvironment {
 public:
  enum class SOCKSVersion {
    V4,
    V5,
  };

  SOCKSConnectJobTest()
      : WithScopedTaskEnvironment(
            base::test::ScopedTaskEnvironment::MainThreadType::MOCK_TIME,
            base::test::ScopedTaskEnvironment::NowSource::
                MAIN_THREAD_MOCK_TIME),
        transport_pool_(2 /* max_sockets */,
                        2 /* max_sockets_per_group */,
                        &host_resolver_,
                        &client_socket_factory_,
                        nullptr /* socket_performance_watcher_factory */,
                        &net_log_) {
    // Set an initial delay to ensure that the first call to TimeTicks::Now()
    // before incrementing the counter does not return a null value.
    FastForwardBy(base::TimeDelta::FromSeconds(1));
  }

  ~SOCKSConnectJobTest() override {}

  static scoped_refptr<SOCKSSocketParams> CreateSOCKSParams(
      SOCKSVersion socks_version) {
    return base::MakeRefCounted<SOCKSSocketParams>(
        base::MakeRefCounted<TransportSocketParams>(
            HostPortPair(kProxyHostName, kProxyPort), true /* respect_limits */,
            OnHostResolutionCallback()),
        socks_version == SOCKSVersion::V5,
        socks_version == SOCKSVersion::V4
            ? HostPortPair(kSOCKS4TestHost, kSOCKS4TestPort)
            : HostPortPair(kSOCKS5TestHost, kSOCKS5TestPort),
        TRAFFIC_ANNOTATION_FOR_TESTS);
  }

 protected:
  NetLog net_log_;
  MockHostResolver host_resolver_;
  MockTaggingClientSocketFactory client_socket_factory_;
  TransportClientSocketPool transport_pool_;
};

TEST_F(SOCKSConnectJobTest, HostResolutionFailure) {
  host_resolver_.rules()->AddSimulatedFailure(kProxyHostName);

  for (bool failure_synchronous : {false, true}) {
    host_resolver_.set_synchronous_mode(failure_synchronous);
    TestConnectJobDelegate test_delegate;
    SOCKSConnectJob socks_connect_job(
        kSOCKS5TestHost /* group_name */, DEFAULT_PRIORITY, SocketTag(),
        true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V5),
        &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
    test_delegate.StartJobExpectingResult(
        &socks_connect_job, ERR_PROXY_CONNECTION_FAILED, failure_synchronous);
  }
}

TEST_F(SOCKSConnectJobTest, HandshakeError) {
  for (bool host_resolution_synchronous : {false, true}) {
    for (bool write_failure_synchronous : {false, true}) {
      host_resolver_.set_synchronous_mode(host_resolution_synchronous);

      // No need to distinguish which part of the handshake fails. Those details
      // are all handled at the StreamSocket layer, not the SOCKSConnectJob.
      MockWrite writes[] = {
          MockWrite(write_failure_synchronous ? SYNCHRONOUS : ASYNC,
                    ERR_UNEXPECTED, 0),
      };
      SequencedSocketData sequenced_socket_data(base::span<MockRead>(), writes);
      // Host resolution is used to switch between sync and async connection
      // behavior. The SOCKS layer can't distinguish between sync and async host
      // resolution vs sync and async connection establishment, so just always
      // make connection establishment synchroonous.
      sequenced_socket_data.set_connect_data(MockConnect(SYNCHRONOUS, OK));
      client_socket_factory_.AddSocketDataProvider(&sequenced_socket_data);

      TestConnectJobDelegate test_delegate;
      SOCKSConnectJob socks_connect_job(
          kSOCKS5TestHost /* group_name */, DEFAULT_PRIORITY, SocketTag(),
          true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V5),
          &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
      test_delegate.StartJobExpectingResult(
          &socks_connect_job, ERR_UNEXPECTED,
          host_resolution_synchronous && write_failure_synchronous);
    }
  }
}

TEST_F(SOCKSConnectJobTest, SOCKS4) {
  for (bool host_resolution_synchronous : {false, true}) {
    for (bool read_and_writes_synchronous : {true}) {
      host_resolver_.set_synchronous_mode(host_resolution_synchronous);

      MockWrite writes[] = {
          MockWrite(SYNCHRONOUS, kSOCKS4OkRequestLocalHostPort80,
                    kSOCKS4OkRequestLocalHostPort80Length, 0),
      };

      MockRead reads[] = {
          MockRead(SYNCHRONOUS, kSOCKS4OkReply, kSOCKS4OkReplyLength, 1),
      };

      SequencedSocketData sequenced_socket_data(reads, writes);
      // Host resolution is used to switch between sync and async connection
      // behavior. The SOCKS layer can't distinguish between sync and async host
      // resolution vs sync and async connection establishment, so just always
      // make connection establishment synchroonous.
      sequenced_socket_data.set_connect_data(MockConnect(SYNCHRONOUS, OK));
      client_socket_factory_.AddSocketDataProvider(&sequenced_socket_data);

      TestConnectJobDelegate test_delegate;
      SOCKSConnectJob socks_connect_job(
          kSOCKS5TestHost /* group_name */, DEFAULT_PRIORITY, SocketTag(),
          true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V4),
          &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
      test_delegate.StartJobExpectingResult(
          &socks_connect_job, OK,
          host_resolution_synchronous && read_and_writes_synchronous);
    }
  }
}

TEST_F(SOCKSConnectJobTest, SOCKS5) {
  for (bool host_resolution_synchronous : {false, true}) {
    for (bool read_and_writes_synchronous : {true}) {
      host_resolver_.set_synchronous_mode(host_resolution_synchronous);

      MockWrite writes[] = {
          MockWrite(SYNCHRONOUS, kSOCKS5GreetRequest, kSOCKS5GreetRequestLength,
                    0),
          MockWrite(SYNCHRONOUS, kSOCKS5OkRequest, kSOCKS5OkRequestLength, 2),
      };

      MockRead reads[] = {
          MockRead(SYNCHRONOUS, kSOCKS5GreetResponse,
                   kSOCKS5GreetResponseLength, 1),
          MockRead(SYNCHRONOUS, kSOCKS5OkResponse, kSOCKS5OkResponseLength, 3),
      };

      SequencedSocketData sequenced_socket_data(reads, writes);
      // Host resolution is used to switch between sync and async connection
      // behavior. The SOCKS layer can't distinguish between sync and async host
      // resolution vs sync and async connection establishment, so just always
      // make connection establishment synchroonous.
      sequenced_socket_data.set_connect_data(MockConnect(SYNCHRONOUS, OK));
      client_socket_factory_.AddSocketDataProvider(&sequenced_socket_data);

      TestConnectJobDelegate test_delegate;
      SOCKSConnectJob socks_connect_job(
          kSOCKS5TestHost /* group_name */, DEFAULT_PRIORITY, SocketTag(),
          true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V5),
          &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
      test_delegate.StartJobExpectingResult(
          &socks_connect_job, OK,
          host_resolution_synchronous && read_and_writes_synchronous);
    }
  }
}

// Check that TransportConnectJob's timeout is respected for the nested
// TransportConnectJob.
TEST_F(SOCKSConnectJobTest, TimeoutDuringDnsResolution) {
  // Set HostResolver to hang.
  host_resolver_.set_ondemand_mode(true);

  TestConnectJobDelegate test_delegate;
  SOCKSConnectJob socks_connect_job(
      kSOCKS5TestHost /* group_name */, DEFAULT_PRIORITY, SocketTag(),
      true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V5),
      &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
  socks_connect_job.Connect();

  // Just before the TransportConnectJob's timeout, nothing should have
  // happened.
  FastForwardBy(TransportConnectJob::ConnectionTimeout() - kTinyTime);
  EXPECT_TRUE(host_resolver_.has_pending_requests());
  EXPECT_FALSE(test_delegate.has_result());

  // Wait for exactly the TransportConnectJob's timeout to have passed. The Job
  // should time out.
  FastForwardBy(kTinyTime);
  EXPECT_FALSE(host_resolver_.has_pending_requests());
  EXPECT_TRUE(test_delegate.has_result());
  EXPECT_THAT(test_delegate.WaitForResult(),
              test::IsError(ERR_PROXY_CONNECTION_FAILED));
}

// Check that SOCKSConnectJob's timeout is respected for the handshake phase.
TEST_F(SOCKSConnectJobTest, TimeoutDuringHandshake) {
  // This test assumes TransportConnectJobs have a shorter timeout than
  // SOCKSConnectJobs.
  ASSERT_LT(TransportConnectJob::ConnectionTimeout(),
            SOCKSConnectJob::ConnectionTimeout());

  host_resolver_.set_ondemand_mode(true);

  MockWrite writes[] = {
      MockWrite(SYNCHRONOUS, ERR_IO_PENDING, 0),
  };

  SequencedSocketData sequenced_socket_data(base::span<MockRead>(), writes);
  sequenced_socket_data.set_connect_data(MockConnect(SYNCHRONOUS, OK));
  client_socket_factory_.AddSocketDataProvider(&sequenced_socket_data);

  TestConnectJobDelegate test_delegate;
  SOCKSConnectJob socks_connect_job(
      kSOCKS5TestHost /* group_name */, DEFAULT_PRIORITY, SocketTag(),
      true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V5),
      &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
  socks_connect_job.Connect();

  // Just before the TransportConnectJob's timeout, nothing should have
  // happened.
  FastForwardBy(TransportConnectJob::ConnectionTimeout() - kTinyTime);
  EXPECT_FALSE(test_delegate.has_result());
  EXPECT_TRUE(host_resolver_.has_pending_requests());

  // DNS resolution completes, and the socket connects.  The request should not
  // time out, even after the TransportConnectJob's timeout passes.
  host_resolver_.ResolveAllPending();

  // The timer is now restarted with a value of
  // SOCKSConnectJob::ConnectionTimeout() -
  // TransportConnectJob::ConnectionTimeout(). Waiting until almost that much
  // time has passed should cause no observable change in the SOCKSConnectJob's
  // status.
  FastForwardBy(SOCKSConnectJob::ConnectionTimeout() -
                TransportConnectJob::ConnectionTimeout() - kTinyTime);
  EXPECT_FALSE(test_delegate.has_result());

  // Wait for exactly the SOCKSConnectJob's timeout has fully elapsed. The Job
  // should time out.
  FastForwardBy(kTinyTime);
  EXPECT_FALSE(host_resolver_.has_pending_requests());
  EXPECT_TRUE(test_delegate.has_result());
  EXPECT_THAT(test_delegate.WaitForResult(), test::IsError(ERR_TIMED_OUT));
}

// Check initial priority is passed to the HostResolver, and priority can be
// modified.
TEST_F(SOCKSConnectJobTest, Priority) {
  host_resolver_.set_ondemand_mode(true);
  // Make resolution eventually fail, so old jobs can easily be removed from the
  // socket pool.
  host_resolver_.rules()->AddSimulatedFailure(kProxyHostName);
  for (int initial_priority = MINIMUM_PRIORITY;
       initial_priority <= MAXIMUM_PRIORITY; ++initial_priority) {
    for (int new_priority = MINIMUM_PRIORITY; new_priority <= MAXIMUM_PRIORITY;
         ++new_priority) {
      // Don't try changing priority to itself, as APIs may not allow that.
      if (new_priority == initial_priority)
        continue;
      TestConnectJobDelegate test_delegate;
      SOCKSConnectJob socks_connect_job(
          kSOCKS5TestHost /* group_name */,
          static_cast<RequestPriority>(initial_priority), SocketTag(),
          true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V4),
          &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
      ASSERT_THAT(socks_connect_job.Connect(), test::IsError(ERR_IO_PENDING));
      ASSERT_TRUE(host_resolver_.has_pending_requests());
      int request_id = host_resolver_.num_resolve();
      EXPECT_EQ(initial_priority, host_resolver_.request_priority(request_id));

      // Change priority.
      socks_connect_job.ChangePriority(
          static_cast<RequestPriority>(new_priority));
      EXPECT_EQ(new_priority, host_resolver_.request_priority(request_id));

      // Restore initial priority.
      socks_connect_job.ChangePriority(
          static_cast<RequestPriority>(initial_priority));
      EXPECT_EQ(initial_priority, host_resolver_.request_priority(request_id));

      // Complete the resolution, which should result in emptying the
      // TransportSocketPool.
      host_resolver_.ResolveAllPending();
      ASSERT_THAT(test_delegate.WaitForResult(),
                  test::IsError(ERR_PROXY_CONNECTION_FAILED));
    }
  }
}

TEST_F(SOCKSConnectJobTest, ConnectTiming) {
  host_resolver_.set_ondemand_mode(true);

  MockWrite writes[] = {
      MockWrite(ASYNC, ERR_IO_PENDING, 0),
      MockWrite(ASYNC, kSOCKS5GreetRequest, kSOCKS5GreetRequestLength, 1),
      MockWrite(SYNCHRONOUS, kSOCKS5OkRequest, kSOCKS5OkRequestLength, 3),
  };

  MockRead reads[] = {
      MockRead(SYNCHRONOUS, kSOCKS5GreetResponse, kSOCKS5GreetResponseLength,
               2),
      MockRead(SYNCHRONOUS, kSOCKS5OkResponse, kSOCKS5OkResponseLength, 4),
  };

  SequencedSocketData sequenced_socket_data(reads, writes);
  // Host resolution is used to switch between sync and async connection
  // behavior. The SOCKS layer can't distinguish between sync and async host
  // resolution vs sync and async connection establishment, so just always
  // make connection establishment synchroonous.
  sequenced_socket_data.set_connect_data(MockConnect(SYNCHRONOUS, OK));
  client_socket_factory_.AddSocketDataProvider(&sequenced_socket_data);

  TestConnectJobDelegate test_delegate;
  SOCKSConnectJob socks_connect_job(
      kSOCKS5TestHost /* group_name */, DEFAULT_PRIORITY, SocketTag(),
      true /* respect_limits */, CreateSOCKSParams(SOCKSVersion::V5),
      &transport_pool_, &host_resolver_, &test_delegate, &net_log_);
  base::TimeTicks start = base::TimeTicks::Now();
  socks_connect_job.Connect();

  // DNS resolution completes after a short delay. The connection should be
  // immediately established as well. The first write to the socket stalls.
  FastForwardBy(kTinyTime);
  host_resolver_.ResolveAllPending();
  RunUntilIdle();

  // After another short delay, data is received from the server.
  FastForwardBy(kTinyTime);
  sequenced_socket_data.Resume();

  EXPECT_THAT(test_delegate.WaitForResult(), test::IsOk());
  // Proxy name resolution is not considered resolving the host name for
  // ConnectionInfo. For SOCKS4, where the host name is also looked up via DNS,
  // the resolution time is not currently reported.
  EXPECT_EQ(base::TimeTicks(), socks_connect_job.connect_timing().dns_start);
  EXPECT_EQ(base::TimeTicks(), socks_connect_job.connect_timing().dns_end);

  // The "connect" time for socks proxies includes DNS resolution time.
  EXPECT_EQ(start, socks_connect_job.connect_timing().connect_start);
  EXPECT_EQ(start + 2 * kTinyTime,
            socks_connect_job.connect_timing().connect_end);

  // Since SSL was not negotiated, SSL times are null.
  EXPECT_EQ(base::TimeTicks(), socks_connect_job.connect_timing().ssl_start);
  EXPECT_EQ(base::TimeTicks(), socks_connect_job.connect_timing().ssl_end);
}

}  // namespace
}  // namespace net
