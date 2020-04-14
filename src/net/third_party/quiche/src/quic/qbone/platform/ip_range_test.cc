// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/ip_range.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace {

TEST(IpRangeTest, TruncateWorksIPv4) {
  QuicIpAddress before_truncate;
  before_truncate.FromString("255.255.255.255");
  EXPECT_EQ("128.0.0.0/1", IpRange(before_truncate, 1).ToString());
  EXPECT_EQ("192.0.0.0/2", IpRange(before_truncate, 2).ToString());
  EXPECT_EQ("255.224.0.0/11", IpRange(before_truncate, 11).ToString());
  EXPECT_EQ("255.255.255.224/27", IpRange(before_truncate, 27).ToString());
  EXPECT_EQ("255.255.255.254/31", IpRange(before_truncate, 31).ToString());
  EXPECT_EQ("255.255.255.255/32", IpRange(before_truncate, 32).ToString());
  EXPECT_EQ("255.255.255.255/32", IpRange(before_truncate, 33).ToString());
}

TEST(IpRangeTest, TruncateWorksIPv6) {
  QuicIpAddress before_truncate;
  before_truncate.FromString("ffff:ffff:ffff:ffff:f903::5");
  EXPECT_EQ("fe00::/7", IpRange(before_truncate, 7).ToString());
  EXPECT_EQ("ffff:ffff:ffff::/48", IpRange(before_truncate, 48).ToString());
  EXPECT_EQ("ffff:ffff:ffff:ffff::/64",
            IpRange(before_truncate, 64).ToString());
  EXPECT_EQ("ffff:ffff:ffff:ffff:8000::/65",
            IpRange(before_truncate, 65).ToString());
  EXPECT_EQ("ffff:ffff:ffff:ffff:f903::4/127",
            IpRange(before_truncate, 127).ToString());
}

TEST(IpRangeTest, FromStringWorksIPv4) {
  IpRange range;
  ASSERT_TRUE(range.FromString("127.0.3.249/26"));
  EXPECT_EQ("127.0.3.192/26", range.ToString());
}

TEST(IpRangeTest, FromStringWorksIPv6) {
  IpRange range;
  ASSERT_TRUE(range.FromString("ff01:8f21:77f9::/33"));
  EXPECT_EQ("ff01:8f21::/33", range.ToString());
}

TEST(IpRangeTest, FirstAddressWorksIPv6) {
  IpRange range;
  ASSERT_TRUE(range.FromString("ffff:ffff::/64"));
  QuicIpAddress first_address = range.FirstAddressInRange();
  EXPECT_EQ("ffff:ffff::", first_address.ToString());
}

TEST(IpRangeTest, FirstAddressWorksIPv4) {
  IpRange range;
  ASSERT_TRUE(range.FromString("10.0.0.0/24"));
  QuicIpAddress first_address = range.FirstAddressInRange();
  EXPECT_EQ("10.0.0.0", first_address.ToString());
}

}  // namespace
}  // namespace quic
