// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/public/util.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace dns_util {

TEST(DnsPublicUtilTest, IsValidDoHTemplate) {
  EXPECT_TRUE(IsValidDoHTemplate(
      "https://dnsserver.example.net/dns-query{?dns}", "GET"));
  EXPECT_TRUE(IsValidDoHTemplate(
      "https://dnsserver.example.net/dns-query{?dns,extra}", "GET"));
  EXPECT_TRUE(IsValidDoHTemplate(
      "https://dnsserver.example.net/dns-query{?dns}", "POST"));
  EXPECT_TRUE(IsValidDoHTemplate(
      "https://dnsserver.example.net/dns-query{?query}", "POST"));
  EXPECT_TRUE(
      IsValidDoHTemplate("https://dnsserver.example.net/dns-query", "POST"));
  EXPECT_TRUE(
      IsValidDoHTemplate("https://query:{dns}@dnsserver.example.net", "GET"));
  EXPECT_TRUE(IsValidDoHTemplate("https://dnsserver.example.net{/dns}", "GET"));
  // Invalid template format
  EXPECT_FALSE(IsValidDoHTemplate(
      "https://dnsserver.example.net/dns-query{{?dns}}", "GET"));
  // Must be HTTPS
  EXPECT_FALSE(
      IsValidDoHTemplate("http://dnsserver.example.net/dns-query", "POST"));
  EXPECT_FALSE(IsValidDoHTemplate(
      "http://dnsserver.example.net/dns-query{?dns}", "GET"));
  // GET requests require the template to have a dns variable
  EXPECT_FALSE(IsValidDoHTemplate(
      "https://dnsserver.example.net/dns-query{?query}", "GET"));
  // Template must expand to a valid URL
  EXPECT_FALSE(IsValidDoHTemplate("https://{?dns}", "GET"));
  // The hostname must not contain the dns variable
  EXPECT_FALSE(IsValidDoHTemplate("https://{dns}.dnsserver.net", "GET"));
}

}  // namespace dns_util
}  // namespace net
