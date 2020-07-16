// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

#include <memory>
#include <sstream>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace {

TEST(QuicSocketAddress, Uninitialized) {
  QuicSocketAddress uninitialized;
  EXPECT_FALSE(uninitialized.IsInitialized());
}

TEST(QuicSocketAddress, ExplicitConstruction) {
  QuicSocketAddress ipv4_address(QuicIpAddress::Loopback4(), 443);
  QuicSocketAddress ipv6_address(QuicIpAddress::Loopback6(), 443);
  EXPECT_TRUE(ipv4_address.IsInitialized());
  EXPECT_EQ("127.0.0.1:443", ipv4_address.ToString());
  EXPECT_EQ("[::1]:443", ipv6_address.ToString());
  EXPECT_EQ(QuicIpAddress::Loopback4(), ipv4_address.host());
  EXPECT_EQ(QuicIpAddress::Loopback6(), ipv6_address.host());
  EXPECT_EQ(443, ipv4_address.port());
}

TEST(QuicSocketAddress, OutputToStream) {
  QuicSocketAddress ipv4_address(QuicIpAddress::Loopback4(), 443);
  std::stringstream stream;
  stream << ipv4_address;
  EXPECT_EQ("127.0.0.1:443", stream.str());
}

TEST(QuicSocketAddress, FromSockaddrIPv4) {
  union {
    sockaddr_storage storage;
    sockaddr addr;
    sockaddr_in v4;
  } address;

  memset(&address, 0, sizeof(address));
  address.v4.sin_family = AF_INET;
  address.v4.sin_addr = QuicIpAddress::Loopback4().GetIPv4();
  address.v4.sin_port = htons(443);
  EXPECT_EQ("127.0.0.1:443",
            QuicSocketAddress(&address.addr, sizeof(address.v4)).ToString());
  EXPECT_EQ("127.0.0.1:443", QuicSocketAddress(address.storage).ToString());
}

TEST(QuicSocketAddress, FromSockaddrIPv6) {
  union {
    sockaddr_storage storage;
    sockaddr addr;
    sockaddr_in6 v6;
  } address;

  memset(&address, 0, sizeof(address));
  address.v6.sin6_family = AF_INET6;
  address.v6.sin6_addr = QuicIpAddress::Loopback6().GetIPv6();
  address.v6.sin6_port = htons(443);
  EXPECT_EQ("[::1]:443",
            QuicSocketAddress(&address.addr, sizeof(address.v6)).ToString());
  EXPECT_EQ("[::1]:443", QuicSocketAddress(address.storage).ToString());
}

TEST(QuicSocketAddres, ToSockaddrIPv4) {
  union {
    sockaddr_storage storage;
    sockaddr_in v4;
  } address;

  address.storage =
      QuicSocketAddress(QuicIpAddress::Loopback4(), 443).generic_address();
  ASSERT_EQ(AF_INET, address.v4.sin_family);
  EXPECT_EQ(QuicIpAddress::Loopback4(), QuicIpAddress(address.v4.sin_addr));
  EXPECT_EQ(htons(443), address.v4.sin_port);
}

TEST(QuicSocketAddress, Normalize) {
  QuicIpAddress dual_stacked;
  ASSERT_TRUE(dual_stacked.FromString("::ffff:127.0.0.1"));
  ASSERT_TRUE(dual_stacked.IsIPv6());
  QuicSocketAddress not_normalized(dual_stacked, 443);
  QuicSocketAddress normalized = not_normalized.Normalized();
  EXPECT_EQ("[::ffff:127.0.0.1]:443", not_normalized.ToString());
  EXPECT_EQ("127.0.0.1:443", normalized.ToString());
}

// TODO(vasilvv): either ensure this works on all platforms, or deprecate and
// remove this API.
#if defined(__linux__) && !defined(ANDROID)
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>

TEST(QuicSocketAddress, FromSocket) {
  int fd;
  QuicSocketAddress address;
  bool bound = false;
  for (int port = 50000; port < 50400; port++) {
    fd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    ASSERT_GT(fd, 0);

    address = QuicSocketAddress(QuicIpAddress::Loopback6(), port);
    sockaddr_storage raw_address = address.generic_address();
    int bind_result = bind(fd, reinterpret_cast<const sockaddr*>(&raw_address),
                           sizeof(sockaddr_in6));

    if (bind_result < 0 && errno == EADDRINUSE) {
      close(fd);
      continue;
    }

    ASSERT_EQ(0, bind_result);
    bound = true;
    break;
  }
  ASSERT_TRUE(bound);

  QuicSocketAddress real_address;
  ASSERT_EQ(0, real_address.FromSocket(fd));
  ASSERT_TRUE(real_address.IsInitialized());
  EXPECT_EQ(real_address, address);
  close(fd);
}
#endif

}  // namespace
}  // namespace quic
