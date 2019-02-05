// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains some tests for TCPClientSocket.
// transport_client_socket_unittest.cc contans some other tests that
// are common for TCP and other types of sockets.

#include "net/socket/tcp_client_socket.h"

#include <stddef.h>

#include "base/test/scoped_task_environment.h"
#include "build/build_config.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/log/net_log_source.h"
#include "net/socket/socket_performance_watcher.h"
#include "net/socket/socket_test_util.h"
#include "net/socket/tcp_server_socket.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/gtest_util.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;
using testing::Not;

namespace base {
class TimeDelta;
}

namespace net {

namespace {

// Try binding a socket to loopback interface and verify that we can
// still connect to a server on the same interface.
TEST(TCPClientSocketTest, BindLoopbackToLoopback) {
  base::test::ScopedTaskEnvironment scoped_task_environment(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  IPAddress lo_address = IPAddress::IPv4Localhost();

  TCPServerSocket server(nullptr, NetLogSource());
  ASSERT_THAT(server.Listen(IPEndPoint(lo_address, 0), 1), IsOk());
  IPEndPoint server_address;
  ASSERT_THAT(server.GetLocalAddress(&server_address), IsOk());

  TCPClientSocket socket(AddressList(server_address), nullptr, nullptr,
                         NetLogSource());

  EXPECT_THAT(socket.Bind(IPEndPoint(lo_address, 0)), IsOk());

  IPEndPoint local_address_result;
  EXPECT_THAT(socket.GetLocalAddress(&local_address_result), IsOk());
  EXPECT_EQ(lo_address, local_address_result.address());

  TestCompletionCallback connect_callback;
  int connect_result = socket.Connect(connect_callback.callback());

  TestCompletionCallback accept_callback;
  std::unique_ptr<StreamSocket> accepted_socket;
  int result = server.Accept(&accepted_socket, accept_callback.callback());
  result = accept_callback.GetResult(result);
  ASSERT_THAT(result, IsOk());

  EXPECT_THAT(connect_callback.GetResult(connect_result), IsOk());

  EXPECT_TRUE(socket.IsConnected());
  socket.Disconnect();
  EXPECT_FALSE(socket.IsConnected());
  EXPECT_EQ(ERR_SOCKET_NOT_CONNECTED,
            socket.GetLocalAddress(&local_address_result));
}

// Try to bind socket to the loopback interface and connect to an
// external address, verify that connection fails.
TEST(TCPClientSocketTest, BindLoopbackToExternal) {
  base::test::ScopedTaskEnvironment scoped_task_environment(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  IPAddress external_ip(72, 14, 213, 105);
  TCPClientSocket socket(AddressList::CreateFromIPAddress(external_ip, 80),
                         NULL, NULL, NetLogSource());

  EXPECT_THAT(socket.Bind(IPEndPoint(IPAddress::IPv4Localhost(), 0)), IsOk());

  TestCompletionCallback connect_callback;
  int result = socket.Connect(connect_callback.callback());

  // We may get different errors here on different system, but
  // connect() is not expected to succeed.
  EXPECT_THAT(connect_callback.GetResult(result), Not(IsOk()));
}

// Bind a socket to the IPv4 loopback interface and try to connect to
// the IPv6 loopback interface, verify that connection fails.
TEST(TCPClientSocketTest, BindLoopbackToIPv6) {
  TCPServerSocket server(NULL, NetLogSource());
  int listen_result =
      server.Listen(IPEndPoint(IPAddress::IPv6Localhost(), 0), 1);
  if (listen_result != OK) {
    LOG(ERROR) << "Failed to listen on ::1 - probably because IPv6 is disabled."
        " Skipping the test";
    return;
  }

  IPEndPoint server_address;
  ASSERT_THAT(server.GetLocalAddress(&server_address), IsOk());
  TCPClientSocket socket(AddressList(server_address), NULL, NULL,
                         NetLogSource());

  EXPECT_THAT(socket.Bind(IPEndPoint(IPAddress::IPv4Localhost(), 0)), IsOk());

  TestCompletionCallback connect_callback;
  int result = socket.Connect(connect_callback.callback());

  EXPECT_THAT(connect_callback.GetResult(result), Not(IsOk()));
}

TEST(TCPClientSocketTest, WasEverUsed) {
  base::test::ScopedTaskEnvironment scoped_task_environment(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  IPAddress lo_address = IPAddress::IPv4Localhost();
  TCPServerSocket server(nullptr, NetLogSource());
  ASSERT_THAT(server.Listen(IPEndPoint(lo_address, 0), 1), IsOk());
  IPEndPoint server_address;
  ASSERT_THAT(server.GetLocalAddress(&server_address), IsOk());

  TCPClientSocket socket(AddressList(server_address), nullptr, nullptr,
                         NetLogSource());

  EXPECT_FALSE(socket.WasEverUsed());

  EXPECT_THAT(socket.Bind(IPEndPoint(lo_address, 0)), IsOk());

  // Just connecting the socket should not set WasEverUsed.
  TestCompletionCallback connect_callback;
  int connect_result = socket.Connect(connect_callback.callback());
  EXPECT_FALSE(socket.WasEverUsed());

  TestCompletionCallback accept_callback;
  std::unique_ptr<StreamSocket> accepted_socket;
  int result = server.Accept(&accepted_socket, accept_callback.callback());
  ASSERT_THAT(accept_callback.GetResult(result), IsOk());
  EXPECT_THAT(connect_callback.GetResult(connect_result), IsOk());

  EXPECT_FALSE(socket.WasEverUsed());
  EXPECT_TRUE(socket.IsConnected());

  // Writing some data to the socket _should_ set WasEverUsed.
  const char kRequest[] = "GET / HTTP/1.0";
  auto write_buffer = base::MakeRefCounted<StringIOBuffer>(kRequest);
  TestCompletionCallback write_callback;
  socket.Write(write_buffer.get(), write_buffer->size(),
               write_callback.callback(), TRAFFIC_ANNOTATION_FOR_TESTS);
  EXPECT_TRUE(socket.WasEverUsed());
  socket.Disconnect();
  EXPECT_FALSE(socket.IsConnected());

  EXPECT_TRUE(socket.WasEverUsed());

  // Re-use the socket, which should set WasEverUsed to false.
  EXPECT_THAT(socket.Bind(IPEndPoint(lo_address, 0)), IsOk());
  TestCompletionCallback connect_callback2;
  connect_result = socket.Connect(connect_callback2.callback());
  EXPECT_FALSE(socket.WasEverUsed());
}

class TestSocketPerformanceWatcher : public SocketPerformanceWatcher {
 public:
  TestSocketPerformanceWatcher() : connection_changed_count_(0u) {}
  ~TestSocketPerformanceWatcher() override = default;

  bool ShouldNotifyUpdatedRTT() const override { return true; }

  void OnUpdatedRTTAvailable(const base::TimeDelta& rtt) override {}

  void OnConnectionChanged() override { connection_changed_count_++; }

  size_t connection_changed_count() const { return connection_changed_count_; }

 private:
  size_t connection_changed_count_;

  DISALLOW_COPY_AND_ASSIGN(TestSocketPerformanceWatcher);
};

// TestSocketPerformanceWatcher requires kernel support for tcp_info struct, and
// so it is enabled only on certain platforms.
#if defined(TCP_INFO) || defined(OS_LINUX)
#define MAYBE_TestSocketPerformanceWatcher TestSocketPerformanceWatcher
#else
#define MAYBE_TestSocketPerformanceWatcher TestSocketPerformanceWatcher
#endif
// Tests if the socket performance watcher is notified if the same socket is
// used for a different connection.
TEST(TCPClientSocketTest, MAYBE_TestSocketPerformanceWatcher) {
  const size_t kNumIPs = 2;
  IPAddressList ip_list;
  for (size_t i = 0; i < kNumIPs; ++i)
    ip_list.push_back(IPAddress(72, 14, 213, i));

  std::unique_ptr<TestSocketPerformanceWatcher> watcher(
      new TestSocketPerformanceWatcher());
  TestSocketPerformanceWatcher* watcher_ptr = watcher.get();

  TCPClientSocket socket(
      AddressList::CreateFromIPAddressList(ip_list, "example.com"),
      std::move(watcher), NULL, NetLogSource());

  EXPECT_THAT(socket.Bind(IPEndPoint(IPAddress::IPv4Localhost(), 0)), IsOk());

  TestCompletionCallback connect_callback;

  ASSERT_NE(OK, connect_callback.GetResult(
                    socket.Connect(connect_callback.callback())));

  EXPECT_EQ(kNumIPs - 1, watcher_ptr->connection_changed_count());
}

// On Android, where socket tagging is supported, verify that
// TCPClientSocket::Tag works as expected.
#if defined(OS_ANDROID)
TEST(TCPClientSocketTest, Tag) {
  base::test::ScopedTaskEnvironment scoped_task_environment(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  // Start test server.
  EmbeddedTestServer test_server;
  test_server.AddDefaultHandlers(base::FilePath());
  ASSERT_TRUE(test_server.Start());

  AddressList addr_list;
  ASSERT_TRUE(test_server.GetAddressList(&addr_list));
  TCPClientSocket s(addr_list, NULL, NULL, NetLogSource());

  // Verify TCP connect packets are tagged and counted properly.
  int32_t tag_val1 = 0x12345678;
  uint64_t old_traffic = GetTaggedBytes(tag_val1);
  SocketTag tag1(SocketTag::UNSET_UID, tag_val1);
  s.ApplySocketTag(tag1);
  TestCompletionCallback connect_callback;
  int connect_result = s.Connect(connect_callback.callback());
  EXPECT_THAT(connect_callback.GetResult(connect_result), IsOk());
  EXPECT_GT(GetTaggedBytes(tag_val1), old_traffic);

  // Verify socket can be retagged with a new value and the current process's
  // UID.
  int32_t tag_val2 = 0x87654321;
  old_traffic = GetTaggedBytes(tag_val2);
  SocketTag tag2(getuid(), tag_val2);
  s.ApplySocketTag(tag2);
  const char kRequest1[] = "GET / HTTP/1.0";
  scoped_refptr<IOBuffer> write_buffer1 =
      base::MakeRefCounted<StringIOBuffer>(kRequest1);
  TestCompletionCallback write_callback1;
  EXPECT_EQ(s.Write(write_buffer1.get(), strlen(kRequest1),
                    write_callback1.callback(), TRAFFIC_ANNOTATION_FOR_TESTS),
            static_cast<int>(strlen(kRequest1)));
  EXPECT_GT(GetTaggedBytes(tag_val2), old_traffic);

  // Verify socket can be retagged with a new value and the current process's
  // UID.
  old_traffic = GetTaggedBytes(tag_val1);
  s.ApplySocketTag(tag1);
  const char kRequest2[] = "\n\n";
  scoped_refptr<IOBufferWithSize> write_buffer2 =
      base::MakeRefCounted<IOBufferWithSize>(strlen(kRequest2));
  memmove(write_buffer2->data(), kRequest2, strlen(kRequest2));
  TestCompletionCallback write_callback2;
  EXPECT_EQ(s.Write(write_buffer2.get(), strlen(kRequest2),
                    write_callback2.callback(), TRAFFIC_ANNOTATION_FOR_TESTS),
            static_cast<int>(strlen(kRequest2)));
  EXPECT_GT(GetTaggedBytes(tag_val1), old_traffic);

  s.Disconnect();
}

TEST(TCPClientSocketTest, TagAfterConnect) {
  base::test::ScopedTaskEnvironment scoped_task_environment(
      base::test::ScopedTaskEnvironment::MainThreadType::IO);

  // Start test server.
  EmbeddedTestServer test_server;
  test_server.AddDefaultHandlers(base::FilePath());
  ASSERT_TRUE(test_server.Start());

  AddressList addr_list;
  ASSERT_TRUE(test_server.GetAddressList(&addr_list));
  TCPClientSocket s(addr_list, NULL, NULL, NetLogSource());

  // Connect socket.
  TestCompletionCallback connect_callback;
  int connect_result = s.Connect(connect_callback.callback());
  EXPECT_THAT(connect_callback.GetResult(connect_result), IsOk());

  // Verify socket can be tagged with a new value and the current process's
  // UID.
  int32_t tag_val2 = 0x87654321;
  uint64_t old_traffic = GetTaggedBytes(tag_val2);
  SocketTag tag2(getuid(), tag_val2);
  s.ApplySocketTag(tag2);
  const char kRequest1[] = "GET / HTTP/1.0";
  scoped_refptr<IOBuffer> write_buffer1 =
      base::MakeRefCounted<StringIOBuffer>(kRequest1);
  TestCompletionCallback write_callback1;
  EXPECT_EQ(s.Write(write_buffer1.get(), strlen(kRequest1),
                    write_callback1.callback(), TRAFFIC_ANNOTATION_FOR_TESTS),
            static_cast<int>(strlen(kRequest1)));
  EXPECT_GT(GetTaggedBytes(tag_val2), old_traffic);

  // Verify socket can be retagged with a new value and the current process's
  // UID.
  int32_t tag_val1 = 0x12345678;
  old_traffic = GetTaggedBytes(tag_val1);
  SocketTag tag1(SocketTag::UNSET_UID, tag_val1);
  s.ApplySocketTag(tag1);
  const char kRequest2[] = "\n\n";
  scoped_refptr<IOBuffer> write_buffer2 =
      base::MakeRefCounted<StringIOBuffer>(kRequest2);
  TestCompletionCallback write_callback2;
  EXPECT_EQ(s.Write(write_buffer2.get(), strlen(kRequest2),
                    write_callback2.callback(), TRAFFIC_ANNOTATION_FOR_TESTS),
            static_cast<int>(strlen(kRequest2)));
  EXPECT_GT(GetTaggedBytes(tag_val1), old_traffic);

  s.Disconnect();
}
#endif

}  // namespace

}  // namespace net
