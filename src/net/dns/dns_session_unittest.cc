// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_session.h"

#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/rand_util.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "net/base/ip_address.h"
#include "net/base/mock_network_change_notifier.h"
#include "net/base/net_errors.h"
#include "net/dns/dns_socket_pool.h"
#include "net/dns/public/dns_protocol.h"
#include "net/dns/resolve_context.h"
#include "net/log/net_log_source.h"
#include "net/socket/socket_performance_watcher.h"
#include "net/socket/socket_test_util.h"
#include "net/socket/ssl_client_socket.h"
#include "net/socket/stream_socket.h"
#include "net/test/test_with_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

class TestClientSocketFactory : public ClientSocketFactory {
 public:
  ~TestClientSocketFactory() override;

  std::unique_ptr<DatagramClientSocket> CreateDatagramClientSocket(
      DatagramSocket::BindType bind_type,
      NetLog* net_log,
      const NetLogSource& source) override;

  std::unique_ptr<TransportClientSocket> CreateTransportClientSocket(
      const AddressList& addresses,
      std::unique_ptr<SocketPerformanceWatcher>,
      NetLog*,
      const NetLogSource&) override {
    NOTIMPLEMENTED();
    return nullptr;
  }

  std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      SSLClientContext* context,
      std::unique_ptr<StreamSocket> stream_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config) override {
    NOTIMPLEMENTED();
    return nullptr;
  }

  std::unique_ptr<ProxyClientSocket> CreateProxyClientSocket(
      std::unique_ptr<StreamSocket> stream_socket,
      const std::string& user_agent,
      const HostPortPair& endpoint,
      const ProxyServer& proxy_server,
      HttpAuthController* http_auth_controller,
      bool tunnel,
      bool using_spdy,
      NextProto negotiated_protocol,
      ProxyDelegate* proxy_delegate,
      const NetworkTrafficAnnotationTag& traffic_annotation) override {
    NOTIMPLEMENTED();
    return nullptr;
  }

 private:
  std::list<std::unique_ptr<SocketDataProvider>> data_providers_;
};

struct PoolEvent {
  enum { ALLOCATE, FREE } action;
  size_t server_index;
};

class DnsSessionTest : public TestWithTaskEnvironment {
 public:
  void OnSocketAllocated(size_t server_index);
  void OnSocketFreed(size_t server_index);

 protected:
  void Initialize(size_t num_servers);
  std::unique_ptr<DnsSession::SocketLease> Allocate(size_t server_index);
  bool DidAllocate(size_t server_index);
  bool DidFree(size_t server_index);
  bool NoMoreEvents();

  DnsConfig config_;
  std::unique_ptr<TestClientSocketFactory> test_client_socket_factory_;
  scoped_refptr<DnsSession> session_;
  NetLogSource source_;

 private:
  bool ExpectEvent(const PoolEvent& event);
  std::list<PoolEvent> events_;
};

class MockDnsSocketPool : public DnsSocketPool {
 public:
  MockDnsSocketPool(ClientSocketFactory* factory, DnsSessionTest* test)
      : DnsSocketPool(factory, base::Bind(&base::RandInt)), test_(test) {}

  ~MockDnsSocketPool() override = default;

  void Initialize(const std::vector<IPEndPoint>* nameservers,
                  NetLog* net_log) override {
    InitializeInternal(nameservers, net_log);
  }

  std::unique_ptr<DatagramClientSocket> AllocateSocket(
      size_t server_index) override {
    test_->OnSocketAllocated(server_index);
    return CreateConnectedSocket(server_index);
  }

  void FreeSocket(size_t server_index,
                  std::unique_ptr<DatagramClientSocket> socket) override {
    test_->OnSocketFreed(server_index);
  }

 private:
  DnsSessionTest* test_;
};

void DnsSessionTest::Initialize(size_t num_servers) {
  CHECK_LT(num_servers, 256u);
  config_.nameservers.clear();
  config_.dns_over_https_servers.clear();
  for (unsigned char i = 0; i < num_servers; ++i) {
    IPEndPoint dns_endpoint(IPAddress(192, 168, 1, i),
                            dns_protocol::kDefaultPort);
    config_.nameservers.push_back(dns_endpoint);
  }

  test_client_socket_factory_.reset(new TestClientSocketFactory());

  DnsSocketPool* dns_socket_pool =
      new MockDnsSocketPool(test_client_socket_factory_.get(), this);

  session_ =
      new DnsSession(config_, std::unique_ptr<DnsSocketPool>(dns_socket_pool),
                     base::Bind(&base::RandInt), nullptr /* NetLog */);

  events_.clear();
}

std::unique_ptr<DnsSession::SocketLease> DnsSessionTest::Allocate(
    size_t server_index) {
  return session_->AllocateSocket(server_index, source_);
}

bool DnsSessionTest::DidAllocate(size_t server_index) {
  PoolEvent expected_event = { PoolEvent::ALLOCATE, server_index };
  return ExpectEvent(expected_event);
}

bool DnsSessionTest::DidFree(size_t server_index) {
  PoolEvent expected_event = { PoolEvent::FREE, server_index };
  return ExpectEvent(expected_event);
}

bool DnsSessionTest::NoMoreEvents() {
  return events_.empty();
}

void DnsSessionTest::OnSocketAllocated(size_t server_index) {
  PoolEvent event = { PoolEvent::ALLOCATE, server_index };
  events_.push_back(event);
}

void DnsSessionTest::OnSocketFreed(size_t server_index) {
  PoolEvent event = { PoolEvent::FREE, server_index };
  events_.push_back(event);
}

bool DnsSessionTest::ExpectEvent(const PoolEvent& expected) {
  if (events_.empty()) {
    return false;
  }

  const PoolEvent actual = events_.front();
  if ((expected.action != actual.action)
      || (expected.server_index != actual.server_index)) {
    return false;
  }
  events_.pop_front();

  return true;
}

std::unique_ptr<DatagramClientSocket>
TestClientSocketFactory::CreateDatagramClientSocket(
    DatagramSocket::BindType bind_type,
    NetLog* net_log,
    const NetLogSource& source) {
  // We're not actually expecting to send or receive any data, so use the
  // simplest SocketDataProvider with no data supplied.
  SocketDataProvider* data_provider = new StaticSocketDataProvider();
  data_providers_.push_back(base::WrapUnique(data_provider));
  std::unique_ptr<MockUDPClientSocket> socket(
      new MockUDPClientSocket(data_provider, net_log));
  return std::move(socket);
}

TestClientSocketFactory::~TestClientSocketFactory() = default;

TEST_F(DnsSessionTest, AllocateFree) {
  std::unique_ptr<DnsSession::SocketLease> lease1, lease2;

  Initialize(2 /* num_servers */);
  EXPECT_TRUE(NoMoreEvents());

  lease1 = Allocate(0);
  EXPECT_TRUE(DidAllocate(0));
  EXPECT_TRUE(NoMoreEvents());

  lease2 = Allocate(1);
  EXPECT_TRUE(DidAllocate(1));
  EXPECT_TRUE(NoMoreEvents());

  lease1.reset();
  EXPECT_TRUE(DidFree(0));
  EXPECT_TRUE(NoMoreEvents());

  lease2.reset();
  EXPECT_TRUE(DidFree(1));
  EXPECT_TRUE(NoMoreEvents());
}

}  // namespace

}  // namespace net
