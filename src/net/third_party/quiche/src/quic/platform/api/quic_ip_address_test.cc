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

  EXPECT_EQ(ip_address, ip_address.Normalized());
  EXPECT_EQ(ip_address, ip_address.DualStacked());
}

TEST(QuicIpAddressTest, FromPackedString) {
  QuicIpAddress loopback4, loopback6;
  const char loopback4_packed[] = "\x7f\0\0\x01";
  const char loopback6_packed[] = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\x01";
  EXPECT_TRUE(loopback4.FromPackedString(loopback4_packed, 4));
  EXPECT_TRUE(loopback6.FromPackedString(loopback6_packed, 16));
  EXPECT_EQ(loopback4, QuicIpAddress::Loopback4());
  EXPECT_EQ(loopback6, QuicIpAddress::Loopback6());
}

TEST(QuicIpAddressTest, MappedAddress) {
  QuicIpAddress ipv4_address;
  QuicIpAddress mapped_address;

  EXPECT_TRUE(ipv4_address.FromString("127.0.0.1"));
  EXPECT_TRUE(mapped_address.FromString("::ffff:7f00:1"));

  EXPECT_EQ(mapped_address, ipv4_address.DualStacked());
  EXPECT_EQ(ipv4_address, mapped_address.Normalized());
}

TEST(QuicIpAddressTest, Subnets) {
  struct {
    const char* address1;
    const char* address2;
    int subnet_size;
    bool same_subnet;
  } test_cases[] = {
      {"127.0.0.1", "127.0.0.2", 24, true},
      {"8.8.8.8", "127.0.0.1", 24, false},
      {"8.8.8.8", "127.0.0.1", 16, false},
      {"8.8.8.8", "127.0.0.1", 8, false},
      {"8.8.8.8", "127.0.0.1", 2, false},
      {"8.8.8.8", "127.0.0.1", 1, true},

      {"127.0.0.1", "127.0.0.128", 24, true},
      {"127.0.0.1", "127.0.0.128", 25, false},
      {"127.0.0.1", "127.0.0.127", 25, true},

      {"127.0.0.1", "127.0.0.0", 30, true},
      {"127.0.0.1", "127.0.0.1", 30, true},
      {"127.0.0.1", "127.0.0.2", 30, true},
      {"127.0.0.1", "127.0.0.3", 30, true},
      {"127.0.0.1", "127.0.0.4", 30, false},

      {"127.0.0.1", "127.0.0.2", 31, false},
      {"127.0.0.1", "127.0.0.0", 31, true},

      {"::1", "fe80::1", 8, false},
      {"::1", "fe80::1", 1, false},
      {"::1", "fe80::1", 0, true},
      {"fe80::1", "fe80::2", 126, true},
      {"fe80::1", "fe80::2", 127, false},
  };

  for (const auto& test_case : test_cases) {
    QuicIpAddress address1, address2;
    ASSERT_TRUE(address1.FromString(test_case.address1));
    ASSERT_TRUE(address2.FromString(test_case.address2));
    EXPECT_EQ(test_case.same_subnet,
              address1.InSameSubnet(address2, test_case.subnet_size))
        << "Addresses: " << test_case.address1 << ", " << test_case.address2
        << "; subnet: /" << test_case.subnet_size;
  }
}

TEST(QuicIpAddress, LoopbackAddresses) {
  QuicIpAddress loopback4;
  QuicIpAddress loopback6;
  ASSERT_TRUE(loopback4.FromString("127.0.0.1"));
  ASSERT_TRUE(loopback6.FromString("::1"));
  EXPECT_EQ(loopback4, QuicIpAddress::Loopback4());
  EXPECT_EQ(loopback6, QuicIpAddress::Loopback6());
}

}  // namespace
}  // namespace test
}  // namespace quic
