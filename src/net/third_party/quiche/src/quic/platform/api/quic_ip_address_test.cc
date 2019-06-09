// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

TEST(QuicIpAddressTest, IPv4) {
  QuicIpAddress ip_address;
  EXPECT_FALSE(ip_address.IsInitialized());

  EXPECT_TRUE(ip_address.FromString("127.0.52.223"));
  EXPECT_TRUE(ip_address.IsInitialized());

  EXPECT_EQ(IpAddressFamily::IP_V4, ip_address.address_family());
  EXPECT_TRUE(ip_address.IsIPv4());
  EXPECT_FALSE(ip_address.IsIPv6());

  EXPECT_EQ("127.0.52.223", ip_address.ToString());
  const in_addr v4_address = ip_address.GetIPv4();
  const uint8_t* const v4_address_ptr =
      reinterpret_cast<const uint8_t*>(&v4_address);
  EXPECT_EQ(127u, *(v4_address_ptr + 0));
  EXPECT_EQ(0u, *(v4_address_ptr + 1));
  EXPECT_EQ(52u, *(v4_address_ptr + 2));
  EXPECT_EQ(223u, *(v4_address_ptr + 3));
}

TEST(QuicIpAddressTest, IPv6) {
  QuicIpAddress ip_address;
  EXPECT_FALSE(ip_address.IsInitialized());

  EXPECT_TRUE(ip_address.FromString("fe80::1ff:fe23:4567"));
  EXPECT_TRUE(ip_address.IsInitialized());

  EXPECT_EQ(IpAddressFamily::IP_V6, ip_address.address_family());
  EXPECT_FALSE(ip_address.IsIPv4());
  EXPECT_TRUE(ip_address.IsIPv6());

  EXPECT_EQ("fe80::1ff:fe23:4567", ip_address.ToString());
  const in6_addr v6_address = ip_address.GetIPv6();
  const uint16_t* const v6_address_ptr =
      reinterpret_cast<const uint16_t*>(&v6_address);
  EXPECT_EQ(0x80feu, *(v6_address_ptr + 0));
  EXPECT_EQ(0x0000u, *(v6_address_ptr + 1));
  EXPECT_EQ(0x0000u, *(v6_address_ptr + 2));
  EXPECT_EQ(0x0000u, *(v6_address_ptr + 3));
  EXPECT_EQ(0x0000u, *(v6_address_ptr + 4));
  EXPECT_EQ(0xff01u, *(v6_address_ptr + 5));
  EXPECT_EQ(0x23feu, *(v6_address_ptr + 6));
  EXPECT_EQ(0x6745u, *(v6_address_ptr + 7));
}

}  // namespace
}  // namespace test
}  // namespace quic
