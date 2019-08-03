// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/ip_pattern.h"

#include "net/base/ip_address.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

bool IsValidPattern(const std::string& pattern_text) {
  IPPattern pattern;
  return pattern.ParsePattern(pattern_text);
}

bool CheckForMatch(const IPPattern& pattern, const std::string& address_text) {
  IPAddress address;
  EXPECT_TRUE(address.AssignFromIPLiteral(address_text));
  return pattern.Match(address);
}

TEST(IPPatternTest, EmptyPattern) {
  IPPattern pattern;
  IPAddress ipv4_address1;
  EXPECT_TRUE(ipv4_address1.AssignFromIPLiteral("1.2.3.4"));
  IPAddress ipv6_address1;
  EXPECT_TRUE(ipv6_address1.AssignFromIPLiteral("1:2:3:4:5:6:7:8"));

  EXPECT_FALSE(pattern.Match(ipv4_address1));
  EXPECT_FALSE(pattern.Match(ipv6_address1));
}

TEST(IPPatternTest, PerfectMatchPattern) {
  IPPattern pattern_v4;
  std::string ipv4_text1("1.2.3.4");
  EXPECT_TRUE(pattern_v4.ParsePattern(ipv4_text1));
  EXPECT_TRUE(pattern_v4.is_ipv4());
  EXPECT_TRUE(CheckForMatch(pattern_v4, ipv4_text1));

  IPPattern pattern_v6;
  std::string ipv6_text1("1:2:3:4:5:6:7:8");
  EXPECT_TRUE(pattern_v6.ParsePattern(ipv6_text1));
  EXPECT_FALSE(pattern_v6.is_ipv4());
  EXPECT_TRUE(CheckForMatch(pattern_v6, ipv6_text1));

  // Also check that there is no confusion betwene v6 and v4, despite having
  // similar values in some sense.
  EXPECT_FALSE(CheckForMatch(pattern_v4, ipv6_text1));
  EXPECT_FALSE(CheckForMatch(pattern_v6, ipv4_text1));
}

TEST(IPPatternTest, AlternativeMatchPattern) {
  IPPattern pattern_v4;
  EXPECT_TRUE(pattern_v4.ParsePattern("1.2.[3,5].4"));
  EXPECT_TRUE(pattern_v4.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.2.2.4"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.3.4"));
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.2.4.4"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.5.4"));
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.2.6.4"));

  IPPattern pattern_v6;
  EXPECT_TRUE(pattern_v6.ParsePattern("1:2fab:3:4:5:[6,8]:7:8"));
  EXPECT_FALSE(pattern_v6.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:5:7:8"));
  EXPECT_TRUE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:6:7:8"));
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:7:7:8"));
  EXPECT_TRUE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:8:7:8"));
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:9:7:8"));
}

TEST(IPPatternTest, RangeMatchPattern) {
  IPPattern pattern_v4;
  EXPECT_TRUE(pattern_v4.ParsePattern("1.2.[3-5].4"));
  EXPECT_TRUE(pattern_v4.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.2.2.4"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.3.4"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.4.4"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.5.4"));
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.2.6.4"));

  IPPattern pattern_v6;
  EXPECT_TRUE(pattern_v6.ParsePattern("1:2fab:3:4:5:[6-8]:7:8"));
  EXPECT_FALSE(pattern_v6.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:5:7:8"));
  EXPECT_TRUE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:6:7:8"));
  EXPECT_TRUE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:7:7:8"));
  EXPECT_TRUE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:8:7:8"));
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:9:7:8"));
}

TEST(IPPatternTest, WildCardMatchPattern) {
  // Use two ranges, and check that only getting both right is a match.
  IPPattern pattern_v4;
  EXPECT_TRUE(pattern_v4.ParsePattern("1.2.*.4"));
  EXPECT_TRUE(pattern_v4.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.2.2.255"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.255.4"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.0.4"));

  IPPattern pattern_v6;
  EXPECT_TRUE(pattern_v6.ParsePattern("1:2fab:3:4:5:*:7:8"));
  EXPECT_FALSE(pattern_v6.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:5:7:8888"));
  EXPECT_TRUE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:FFFF:7:8"));
  EXPECT_TRUE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:9999:7:8"));
}

TEST(IPPatternTest, MultiRangeMatchPattern) {
  // Use two ranges, and check that only getting both right is a match.
  // This ensures that the right component range is matched against the desired
  // component.
  IPPattern pattern_v4;
  EXPECT_TRUE(pattern_v4.ParsePattern("1.[2-3].3.[4-5]"));
  EXPECT_TRUE(pattern_v4.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.4.3.6"));
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.2.3.6"));
  EXPECT_FALSE(CheckForMatch(pattern_v4, "1.4.3.4"));
  EXPECT_TRUE(CheckForMatch(pattern_v4, "1.2.3.4"));

  IPPattern pattern_v6;
  EXPECT_TRUE(pattern_v6.ParsePattern("1:2fab:3:4:[5-7]:6:7:[8-A]"));
  EXPECT_FALSE(pattern_v6.is_ipv4());
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:4:5:7:F"));
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:5:5:7:F"));
  EXPECT_FALSE(CheckForMatch(pattern_v6, "1:2fab:3:4:4:6:7:A"));
  EXPECT_TRUE(CheckForMatch(pattern_v6,  "1:2fab:3:4:5:6:7:A"));
}

TEST(IPPatternTest, BytoOrderInIPv6Ranges) {
  IPPattern pattern_v6_low_byte;
  EXPECT_TRUE(pattern_v6_low_byte.ParsePattern("1:2:3:4:5:6:7:[0-FF]"));
  EXPECT_TRUE(CheckForMatch(pattern_v6_low_byte, "1:2:3:4:5:6:7:0088"));
  EXPECT_FALSE(CheckForMatch(pattern_v6_low_byte, "1:2:3:4:5:6:7:8800"));

  IPPattern pattern_v6_high_byte;
  EXPECT_TRUE(pattern_v6_high_byte.ParsePattern("1:2:3:4:5:6:7:[0-FF00]"));
  EXPECT_TRUE(CheckForMatch(pattern_v6_high_byte, "1:2:3:4:5:6:7:0088"));
  EXPECT_TRUE(CheckForMatch(pattern_v6_high_byte, "1:2:3:4:5:6:7:FF00"));
  EXPECT_FALSE(CheckForMatch(pattern_v6_high_byte, "1:2:3:4:5:6:7:FF01"));
}

TEST(IPPatternTest, InvalidPatterns) {
  EXPECT_FALSE(IsValidPattern("1:2:3:4:5:6:7:8:9"));  // Too long.
  EXPECT_FALSE(IsValidPattern("1:2:3:4:5:6:7"));      // Too Short
  EXPECT_FALSE(IsValidPattern("1:2:3:4:5:6:7:H"));    // Non-hex.
  EXPECT_FALSE(IsValidPattern("1:G:3:4:5:6:7:8"));    // Non-hex.

  EXPECT_FALSE(IsValidPattern("1.2.3.4.5"));  // Too long
  EXPECT_FALSE(IsValidPattern("1.2.3"));  // Too short
  EXPECT_FALSE(IsValidPattern("1.2.3.A"));  // Non-decimal.
  EXPECT_FALSE(IsValidPattern("1.A.3.4"));  // Non-decimal
  EXPECT_FALSE(IsValidPattern("1.256.3.4"));  // Out of range
}

}  // namespace

}  // namespace net
