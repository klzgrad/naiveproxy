// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/pac_library.h"

#include "net/base/address_list.h"
#include "net/base/net_errors.h"
#include "net/base/network_interfaces.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/datagram_client_socket.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

// Helper for verifying whether the address list returned by myIpAddress() /
// myIpAddressEx() looks correct.
void VerifyActualMyIpAddresses(const IPAddressList& test_list) {
  // Enumerate all of the IP addresses for the system (skipping loopback and
  // link-local ones). This is used as a reference implementation to check
  // whether |test_list| (which was obtained using a different strategy) looks
  // correct.
  std::set<IPAddress> candidates;
  NetworkInterfaceList networks;
  GetNetworkList(&networks, EXCLUDE_HOST_SCOPE_VIRTUAL_INTERFACES);
  for (const auto& network : networks) {
    if (network.address.IsLinkLocal() || network.address.IsLoopback())
      continue;
    candidates.insert(network.address);
  }

  // Ordinarily the machine running this test will have an IP address. However
  // for some bot configurations (notably Android) that may not be the case.
  EXPECT_EQ(candidates.empty(), test_list.empty());

  // |test_list| should be a subset of |candidates|.
  for (const auto& ip : test_list)
    EXPECT_EQ(1u, candidates.count(ip));
}

// Tests for PacMyIpAddress() and PacMyIpAddressEx().
TEST(PacLibraryTest, ActualPacMyIpAddress) {
  auto my_ip_addresses = PacMyIpAddress();

  VerifyActualMyIpAddresses(my_ip_addresses);
}

TEST(PacLibraryTest, ActualPacMyIpAddressEx) {
  VerifyActualMyIpAddresses(PacMyIpAddressEx());
}

IPAddress CreateIPAddress(base::StringPiece literal) {
  IPAddress result;
  if (!result.AssignFromIPLiteral(literal)) {
    ADD_FAILURE() << "Failed parsing IP: " << literal;
    return IPAddress();
  }
  return result;
}

AddressList CreateAddressList(
    const std::vector<base::StringPiece>& ip_literals) {
  AddressList result;
  for (const auto& ip : ip_literals)
    result.push_back(IPEndPoint(CreateIPAddress(ip), 8080));
  return result;
}

class MockUDPSocket : public DatagramClientSocket {
 public:
  MockUDPSocket(const IPAddress& peer_ip,
                const IPAddress& local_ip,
                Error connect_error)
      : peer_ip_(peer_ip), local_ip_(local_ip), connect_error_(connect_error) {}

  ~MockUDPSocket() override = default;

  // Socket implementation.
  int Read(IOBuffer* buf,
           int buf_len,
           CompletionOnceCallback callback) override {
    ADD_FAILURE() << "Called Read()";
    return ERR_UNEXPECTED;
  }
  int Write(IOBuffer* buf,
            int buf_len,
            CompletionOnceCallback callback,
            const NetworkTrafficAnnotationTag& traffic_annotation) override {
    ADD_FAILURE() << "Called Read()";
    return ERR_UNEXPECTED;
  }
  int WriteAsync(
      DatagramBuffers buffers,
      CompletionOnceCallback callback,
      const NetworkTrafficAnnotationTag& traffic_annotation) override {
    ADD_FAILURE() << "Called WriteAsync()";
    return ERR_UNEXPECTED;
  }
  int WriteAsync(
      const char* buffer,
      size_t buf_len,
      CompletionOnceCallback callback,
      const NetworkTrafficAnnotationTag& traffic_annotation) override {
    ADD_FAILURE() << "Called WriteAsync()";
    return ERR_UNEXPECTED;
  }
  DatagramBuffers GetUnwrittenBuffers() override {
    ADD_FAILURE() << "Called GetUnwrittenBuffers()";
    return DatagramBuffers();
  }
  int SetReceiveBufferSize(int32_t size) override {
    ADD_FAILURE() << "Called SetReceiveBufferSize()";
    return ERR_UNEXPECTED;
  }
  int SetSendBufferSize(int32_t size) override {
    ADD_FAILURE() << "Called SetSendBufferSize()";
    return ERR_UNEXPECTED;
  }
  int SetDoNotFragment() override {
    ADD_FAILURE() << "Called SetDoNotFragment()";
    return ERR_UNEXPECTED;
  }
  // DatagramSocket implementation.
  void Close() override { ADD_FAILURE() << "Called Close()"; }
  int GetPeerAddress(IPEndPoint* address) const override {
    ADD_FAILURE() << "Called GetPeerAddress()";
    return ERR_UNEXPECTED;
  }
  int GetLocalAddress(IPEndPoint* address) const override {
    if (connect_error_ != OK)
      return connect_error_;

    *address = IPEndPoint(local_ip_, 8080);
    return OK;
  }
  void UseNonBlockingIO() override {
    ADD_FAILURE() << "Called UseNonBlockingIO()";
  }
  void SetWriteAsyncEnabled(bool enabled) override {
    ADD_FAILURE() << "Called SetWriteAsyncEnabled()";
  }
  void SetMaxPacketSize(size_t max_packet_size) override {
    ADD_FAILURE() << "Called SetWriteAsyncEnabled()";
  }
  bool WriteAsyncEnabled() override {
    ADD_FAILURE() << "Called WriteAsyncEnabled()";
    return false;
  }
  void SetWriteMultiCoreEnabled(bool enabled) override {
    ADD_FAILURE() << "Called SetWriteMultiCoreEnabled()";
  }
  void SetSendmmsgEnabled(bool enabled) override {
    ADD_FAILURE() << "Called SetSendmmsgEnabled()";
  }
  void SetWriteBatchingActive(bool active) override {
    ADD_FAILURE() << "Called SetWriteBatchingActive()";
  }
  const NetLogWithSource& NetLog() const override {
    ADD_FAILURE() << "Called NetLog()";
    return net_log_;
  }

  // DatagramClientSocket implementation.
  int Connect(const IPEndPoint& address) override {
    EXPECT_EQ(peer_ip_.ToString(), address.address().ToString());
    return connect_error_;
  }
  int ConnectUsingNetwork(NetworkChangeNotifier::NetworkHandle network,
                          const IPEndPoint& address) override {
    ADD_FAILURE() << "Called ConnectUsingNetwork()";
    return ERR_UNEXPECTED;
  }
  int ConnectUsingDefaultNetwork(const IPEndPoint& address) override {
    ADD_FAILURE() << "Called ConnectUsingDefaultNetwork()";
    return ERR_UNEXPECTED;
  }
  NetworkChangeNotifier::NetworkHandle GetBoundNetwork() const override {
    ADD_FAILURE() << "Called GetBoundNetwork()";
    return network_;
  }
  void ApplySocketTag(const SocketTag& tag) override {
    ADD_FAILURE() << "Called ApplySocketTag()";
  }
  void SetMsgConfirm(bool confirm) override {
    ADD_FAILURE() << "Called SetMsgConfirm()";
  }

 private:
  NetLogWithSource net_log_;
  NetworkChangeNotifier::NetworkHandle network_;

  IPAddress peer_ip_;
  IPAddress local_ip_;
  Error connect_error_;

  DISALLOW_COPY_AND_ASSIGN(MockUDPSocket);
};

class MockSocketFactory : public ClientSocketFactory {
 public:
  MockSocketFactory() = default;

  void AddUDPConnectSuccess(base::StringPiece peer_ip_literal,
                            base::StringPiece local_ip_literal) {
    auto peer_ip = CreateIPAddress(peer_ip_literal);
    auto local_ip = CreateIPAddress(local_ip_literal);

    // The address family of local and peer IP must match.
    ASSERT_EQ(peer_ip.size(), local_ip.size());

    udp_sockets_.push_back(std::make_unique<MockUDPSocket>(
        peer_ip, local_ip, OK));
  }

  void AddUDPConnectFailure(base::StringPiece peer_ip) {
    udp_sockets_.push_back(std::make_unique<MockUDPSocket>(
        CreateIPAddress(peer_ip), IPAddress(), ERR_ADDRESS_UNREACHABLE));
  }

  ~MockSocketFactory() override {
    EXPECT_EQ(0u, udp_sockets_.size())
        << "Not all of the mock sockets were consumed.";
  }

  // ClientSocketFactory
  std::unique_ptr<DatagramClientSocket> CreateDatagramClientSocket(
      DatagramSocket::BindType bind_type,
      NetLog* net_log,
      const NetLogSource& source) override {
    if (udp_sockets_.empty()) {
      ADD_FAILURE() << "Not enough mock UDP sockets";
      return nullptr;
    }

    auto result = std::move(udp_sockets_.front());
    udp_sockets_.erase(udp_sockets_.begin());
    return result;
  }
  std::unique_ptr<TransportClientSocket> CreateTransportClientSocket(
      const AddressList& addresses,
      std::unique_ptr<SocketPerformanceWatcher> socket_performance_watcher,
      NetLog* net_log,
      const NetLogSource& source) override {
    ADD_FAILURE() << "Called CreateTransportClientSocket()";
    return nullptr;
  }
  std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config,
      const SSLClientSocketContext& context) override {
    ADD_FAILURE() << "Called CreateSSLClientSocket()";
    return nullptr;
  }
  std::unique_ptr<ProxyClientSocket> CreateProxyClientSocket(
      std::unique_ptr<ClientSocketHandle> transport_socket,
      const std::string& user_agent,
      const HostPortPair& endpoint,
      HttpAuthController* http_auth_controller,
      bool tunnel,
      bool using_spdy,
      NextProto negotiated_protocol,
      bool is_https_proxy,
      const NetworkTrafficAnnotationTag& traffic_annotation) override {
    ADD_FAILURE() << "Called CreateProxyClientSocket()";
    return nullptr;
  }
  void ClearSSLSessionCache() override {
    ADD_FAILURE() << "Called ClearSSLSessionCache()";
  }

 private:
  std::vector<std::unique_ptr<MockUDPSocket>> udp_sockets_;

  DISALLOW_COPY_AND_ASSIGN(MockSocketFactory);
};

// Tests myIpAddress() when there is a route to 8.8.8.8.
TEST(PacLibraryTest, PacMyIpAddress8888) {
  MockSocketFactory factory;
  factory.AddUDPConnectSuccess("8.8.8.8", "192.168.1.1");

  auto result = PacMyIpAddressForTest(&factory, {});
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("192.168.1.1", result.front().ToString());
}

// Tests myIpAddress() when there is no route to 8.8.8.8, but there is one to
// 2001:4860:4860::8888.
TEST(PacLibraryTest, PacMyIpAddress2001) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectSuccess("2001:4860:4860::8888", "2001::beef");

  AddressList dns_result;

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("2001::beef", result.front().ToString());
}

// Tests myIpAddress() when there is no route to 8.8.8.8, no route to
// 2001:4860:4860::8888, however getaddrinfo(gethostname()) finds results. Most
// of those results are skipped over, and the IPv4 one is favored.
TEST(PacLibraryTest, PacMyIpAddressHostname) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result = CreateAddressList({
      "169.254.13.16", "127.0.0.1", "::1", "fe89::beef", "2001::f001",
      "178.1.99.3", "192.168.1.3",
  });

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("178.1.99.3", result.front().ToString());
}

// Tests myIpAddress() when there is no route to 8.8.8.8, no route to
// 2001:4860:4860::8888, however getaddrinfo(gethostname()) finds multiple IPv6
// results.
TEST(PacLibraryTest, PacMyIpAddressHostnameAllIPv6) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result =
      CreateAddressList({"::1", "2001::f001", "2001::f00d", "169.254.0.6"});

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("2001::f001", result.front().ToString());
}

// Tests myIpAddress() when there is no route to 8.8.8.8, no route to
// 2001:4860:4860::8888, no acceptable result in getaddrinfo(gethostname()),
// however there is a route for private address.
TEST(PacLibraryTest, PacMyIpAddressPrivateIPv4) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result = CreateAddressList({
      "169.254.13.16", "127.0.0.1", "::1", "fe89::beef",
  });

  factory.AddUDPConnectSuccess("10.0.0.0", "127.0.0.1");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectSuccess("192.168.0.0", "63.31.9.8");

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("63.31.9.8", result.front().ToString());
}

// Tests myIpAddress() when there is no route to 8.8.8.8, no route to
// 2001:4860:4860::8888, no acceptable result in getaddrinfo(gethostname()),
// however there is a route for private address.
TEST(PacLibraryTest, PacMyIpAddressPrivateIPv6) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result;

  factory.AddUDPConnectSuccess("10.0.0.0", "127.0.0.1");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectSuccess("FC00::", "2001::7777");

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("2001::7777", result.front().ToString());
}

// Tests myIpAddress() when there are no routes, and getaddrinfo(gethostname())
// fails.
TEST(PacLibraryTest, PacMyIpAddressAllFail) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result;

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectFailure("FC00::");

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  EXPECT_EQ(0u, result.size());
}

// Tests myIpAddress() when there are no routes, and
// getaddrinfo(gethostname()) only returns loopback.
TEST(PacLibraryTest, PacMyIpAddressAllFailOrLoopback) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result = CreateAddressList({"127.0.0.1", "::1"});

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectFailure("FC00::");

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  EXPECT_EQ(0u, result.size());
}

// Tests myIpAddress() when there is only an IPv6 link-local address.
TEST(PacLibraryTest, PacMyIpAddressAllFailHasLinkLocal) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result =
      CreateAddressList({"127.0.0.1", "::1", "fe81::8881"});

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectFailure("FC00::");

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("fe81::8881", result.front().ToString());
}

// Tests myIpAddress() when there are only link-local addresses. The IPv4
// link-local address is favored.
TEST(PacLibraryTest, PacMyIpAddressAllFailHasLinkLocalFavorIPv4) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result =
      CreateAddressList({"127.0.0.1", "::1", "fe81::8881", "169.254.89.133"});

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectFailure("FC00::");

  auto result = PacMyIpAddressForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("169.254.89.133", result.front().ToString());
}

// Tests myIpAddressEx() when there is a route to 8.8.8.8 but not one to
// 2001:4860:4860::8888
TEST(PacLibraryTest, PacMyIpAddressEx8888) {
  MockSocketFactory factory;
  factory.AddUDPConnectSuccess("8.8.8.8", "192.168.1.1");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  auto result = PacMyIpAddressExForTest(&factory, {});
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("192.168.1.1", result.front().ToString());
}

// Tests myIpAddressEx() when there is a route to 2001:4860:4860::8888 but
// not 8.8.8.8.
TEST(PacLibraryTest, PacMyIpAddressEx2001) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectSuccess("2001:4860:4860::8888", "2001::3333");

  AddressList dns_result;

  auto result = PacMyIpAddressExForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("2001::3333", result.front().ToString());
}

// Tests myIpAddressEx() when there is a route to both 8.8.8.8 and
// 2001:4860:4860::8888.
TEST(PacLibraryTest, PacMyIpAddressEx8888And2001) {
  MockSocketFactory factory;
  factory.AddUDPConnectSuccess("8.8.8.8", "192.168.17.8");
  factory.AddUDPConnectSuccess("2001:4860:4860::8888", "2001::8333");

  AddressList dns_result;

  auto result = PacMyIpAddressExForTest(&factory, dns_result);
  ASSERT_EQ(2u, result.size());
  EXPECT_EQ("192.168.17.8", result.front().ToString());
  EXPECT_EQ("2001::8333", result.back().ToString());
}

// Tests myIpAddressEx() when there is no route to 8.8.8.8, no route to
// 2001:4860:4860::8888, however getaddrinfo(gethostname()) finds results. Some
// of those results are skipped due to being link-local and loopback.
TEST(PacLibraryTest, PacMyIpAddressExHostname) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result = CreateAddressList({
      "169.254.13.16", "::1", "fe89::beef", "2001::bebe", "178.1.99.3",
      "127.0.0.1", "192.168.1.3",
  });

  auto result = PacMyIpAddressExForTest(&factory, dns_result);
  ASSERT_EQ(3u, result.size());
  EXPECT_EQ("2001::bebe", result[0].ToString());
  EXPECT_EQ("178.1.99.3", result[1].ToString());
  EXPECT_EQ("192.168.1.3", result[2].ToString());
}

// Tests myIpAddressEx() when routes are found for private IP space.
TEST(PacLibraryTest, PacMyIpAddressExPrivateDuplicates) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result;

  factory.AddUDPConnectSuccess("10.0.0.0", "192.168.3.3");
  factory.AddUDPConnectSuccess("172.16.0.0", "192.168.3.4");
  factory.AddUDPConnectSuccess("192.168.0.0", "192.168.3.3");
  factory.AddUDPConnectSuccess("FC00::", "2001::beef");

  auto result = PacMyIpAddressExForTest(&factory, dns_result);

  // Note that 192.168.3.3. was probed twice, but only added once to the final
  // result.
  ASSERT_EQ(3u, result.size());
  EXPECT_EQ("192.168.3.3", result[0].ToString());
  EXPECT_EQ("192.168.3.4", result[1].ToString());
  EXPECT_EQ("2001::beef", result[2].ToString());
}

// Tests myIpAddressEx() when there are no routes, and
// getaddrinfo(gethostname()) fails.
TEST(PacLibraryTest, PacMyIpAddressExAllFail) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result;

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectFailure("FC00::");

  auto result = PacMyIpAddressExForTest(&factory, dns_result);
  EXPECT_EQ(0u, result.size());
}

// Tests myIpAddressEx() when there are only IPv6 link-local address.
TEST(PacLibraryTest, PacMyIpAddressExAllFailHasLinkLocal) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result =
      CreateAddressList({"127.0.0.1", "::1", "fe81::8881", "fe80::8899"});

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectSuccess("FC00::", "fe80::1");

  auto result = PacMyIpAddressExForTest(&factory, dns_result);
  // There were four link-local addresses found, but only the first one is
  // returned.
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("fe81::8881", result.front().ToString());
}

// Tests myIpAddressEx() when there are only link-local addresses. The IPv4
// link-local address is favored.
TEST(PacLibraryTest, PacMyIpAddressExAllFailHasLinkLocalFavorIPv4) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result =
      CreateAddressList({"127.0.0.1", "::1", "fe81::8881", "169.254.89.133"});

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectFailure("FC00::");

  auto result = PacMyIpAddressExForTest(&factory, dns_result);
  ASSERT_EQ(1u, result.size());
  EXPECT_EQ("169.254.89.133", result.front().ToString());
}

// Tests myIpAddressEx() when there are no routes, and
// getaddrinfo(gethostname()) only returns loopback.
TEST(PacLibraryTest, PacMyIpAddressExAllFailOrLoopback) {
  MockSocketFactory factory;
  factory.AddUDPConnectFailure("8.8.8.8");
  factory.AddUDPConnectFailure("2001:4860:4860::8888");

  AddressList dns_result = CreateAddressList({"127.0.0.1", "::1"});

  factory.AddUDPConnectFailure("10.0.0.0");
  factory.AddUDPConnectFailure("172.16.0.0");
  factory.AddUDPConnectFailure("192.168.0.0");
  factory.AddUDPConnectFailure("FC00::");

  auto result = PacMyIpAddressExForTest(&factory, dns_result);
  EXPECT_EQ(0u, result.size());
}

}  // namespace
}  // namespace net
