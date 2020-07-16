// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/host_mapping_rules.h"

#include "net/base/host_port_pair.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

TEST(HostMappingRulesTest, SetRulesFromString) {
  HostMappingRules rules;
  rules.SetRulesFromString(
      "map *.com baz , map *.net bar:60, EXCLUDE *.foo.com");

  HostPortPair host_port("test", 1234);
  EXPECT_FALSE(rules.RewriteHost(&host_port));
  EXPECT_EQ("test", host_port.host());
  EXPECT_EQ(1234u, host_port.port());

  host_port = HostPortPair("chrome.net", 80);
  EXPECT_TRUE(rules.RewriteHost(&host_port));
  EXPECT_EQ("bar", host_port.host());
  EXPECT_EQ(60u, host_port.port());

  host_port = HostPortPair("crack.com", 80);
  EXPECT_TRUE(rules.RewriteHost(&host_port));
  EXPECT_EQ("baz", host_port.host());
  EXPECT_EQ(80u, host_port.port());

  host_port = HostPortPair("wtf.foo.com", 666);
  EXPECT_FALSE(rules.RewriteHost(&host_port));
  EXPECT_EQ("wtf.foo.com", host_port.host());
  EXPECT_EQ(666u, host_port.port());
}

TEST(HostMappingRulesTest, PortSpecificMatching) {
  HostMappingRules rules;
  rules.SetRulesFromString(
      "map *.com:80 baz:111 , map *.com:443 blat:333, EXCLUDE *.foo.com");

  // No match
  HostPortPair host_port("test.com", 1234);
  EXPECT_FALSE(rules.RewriteHost(&host_port));
  EXPECT_EQ("test.com", host_port.host());
  EXPECT_EQ(1234u, host_port.port());

  // Match port 80
  host_port = HostPortPair("crack.com", 80);
  EXPECT_TRUE(rules.RewriteHost(&host_port));
  EXPECT_EQ("baz", host_port.host());
  EXPECT_EQ(111u, host_port.port());

  // Match port 443
  host_port = HostPortPair("wtf.com", 443);
  EXPECT_TRUE(rules.RewriteHost(&host_port));
  EXPECT_EQ("blat", host_port.host());
  EXPECT_EQ(333u, host_port.port());

  // Match port 443, but excluded.
  host_port = HostPortPair("wtf.foo.com", 443);
  EXPECT_FALSE(rules.RewriteHost(&host_port));
  EXPECT_EQ("wtf.foo.com", host_port.host());
  EXPECT_EQ(443u, host_port.port());
}

// Parsing bad rules should silently discard the rule (and never crash).
TEST(HostMappingRulesTest, ParseInvalidRules) {
  HostMappingRules rules;

  EXPECT_FALSE(rules.AddRuleFromString("xyz"));
  EXPECT_FALSE(rules.AddRuleFromString(std::string()));
  EXPECT_FALSE(rules.AddRuleFromString(" "));
  EXPECT_FALSE(rules.AddRuleFromString("EXCLUDE"));
  EXPECT_FALSE(rules.AddRuleFromString("EXCLUDE foo bar"));
  EXPECT_FALSE(rules.AddRuleFromString("INCLUDE"));
  EXPECT_FALSE(rules.AddRuleFromString("INCLUDE x"));
  EXPECT_FALSE(rules.AddRuleFromString("INCLUDE x :10"));
}

}  // namespace

}  // namespace net
