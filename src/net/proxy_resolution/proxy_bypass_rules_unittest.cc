// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/proxy_bypass_rules.h"

#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "net/proxy_resolution/proxy_config_service_common_unittest.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

// Calls |MatchesImplicitRules()| for each name in |hosts| (for various URL
// schemes), and checks that the result is |bypasses|.
template <size_t N>
void ExpectMatchesImplicitRules(const char* (&hosts)[N], bool bypasses) {
  // The scheme of the URL shouldn't matter.
  const char* kUrlSchemes[] = {"http://", "https://", "ftp://"};

  for (auto* scheme : kUrlSchemes) {
    for (size_t i = 0; i < N; ++i) {
      const char* host = hosts[i];
      std::string url = std::string(scheme) + std::string(host);
      EXPECT_EQ(bypasses, ProxyBypassRules::MatchesImplicitRules(GURL(url)))
          << url;
    }
  }
}

TEST(ProxyBypassRulesTest, ParseAndMatchBasicHost) {
  ProxyBypassRules rules;
  rules.ParseFromString("wWw.gOogle.com");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("www.google.com", rules.rules()[0]->ToString());

  // All of these match; port, scheme, and non-hostname components don't
  // matter.
  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com")));
  EXPECT_TRUE(rules.Matches(GURL("ftp://www.google.com:99")));
  EXPECT_TRUE(rules.Matches(GURL("https://www.google.com:81")));

  // Must be a strict host match to work.
  EXPECT_FALSE(rules.Matches(GURL("http://foo.www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://xxx.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com.baz.org")));
}

TEST(ProxyBypassRulesTest, ParseAndMatchBasicDomain) {
  ProxyBypassRules rules;
  rules.ParseFromString(".gOOgle.com");
  ASSERT_EQ(1u, rules.rules().size());
  // Note that we inferred this was an "ends with" test.
  EXPECT_EQ("*.google.com", rules.rules()[0]->ToString());

  // All of these match; port, scheme, and non-hostname components don't
  // matter.
  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com")));
  EXPECT_TRUE(rules.Matches(GURL("ftp://www.google.com:99")));
  EXPECT_TRUE(rules.Matches(GURL("https://a.google.com:81")));
  EXPECT_TRUE(rules.Matches(GURL("http://foo.google.com/x/y?q")));
  EXPECT_TRUE(rules.Matches(GURL("http://foo:bar@baz.google.com#x")));

  // Must be a strict "ends with" to work.
  EXPECT_FALSE(rules.Matches(GURL("http://google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://foo.google.com.baz.org")));
}

TEST(ProxyBypassRulesTest, ParseAndMatchBasicDomainWithPort) {
  ProxyBypassRules rules;
  rules.ParseFromString("*.GOOGLE.com:80");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("*.google.com:80", rules.rules()[0]->ToString());

  // All of these match; scheme, and non-hostname components don't matter.
  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com")));
  EXPECT_TRUE(rules.Matches(GURL("ftp://www.google.com:80")));
  EXPECT_TRUE(rules.Matches(GURL("https://a.google.com:80?x")));

  // Must be a strict "ends with" to work.
  EXPECT_FALSE(rules.Matches(GURL("http://google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://foo.google.com.baz.org")));

  // The ports must match.
  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com:90")));
  EXPECT_FALSE(rules.Matches(GURL("https://www.google.com")));
}

TEST(ProxyBypassRulesTest, MatchAll) {
  ProxyBypassRules rules;
  rules.ParseFromString("*");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("*", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com")));
  EXPECT_TRUE(rules.Matches(GURL("ftp://www.foobar.com:99")));
  EXPECT_TRUE(rules.Matches(GURL("https://a.google.com:80?x")));
}

TEST(ProxyBypassRulesTest, WildcardAtStart) {
  ProxyBypassRules rules;
  rules.ParseFromString("*.org:443");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("*.org:443", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://www.google.org:443")));
  EXPECT_TRUE(rules.Matches(GURL("https://www.google.org")));

  EXPECT_FALSE(rules.Matches(GURL("http://www.google.org")));
  EXPECT_FALSE(rules.Matches(GURL("https://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("https://www.google.org.com")));
}

// Tests a codepath that parses hostnamepattern:port, where "port" is invalid
// by containing a leading plus.
TEST(ProxyBypassRulesTest, ParseInvalidPort) {
  ProxyBypassRules rules;
  EXPECT_TRUE(rules.AddRuleFromString("*.org:443"));
  EXPECT_FALSE(rules.AddRuleFromString("*.com:+443"));
  EXPECT_FALSE(rules.AddRuleFromString("*.com:-443"));
}

TEST(ProxyBypassRulesTest, IPV4Address) {
  ProxyBypassRules rules;
  rules.ParseFromString("192.168.1.1");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("192.168.1.1", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://192.168.1.1")));
  EXPECT_TRUE(rules.Matches(GURL("https://192.168.1.1:90")));

  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://sup.192.168.1.1")));
}

TEST(ProxyBypassRulesTest, IPV4AddressWithPort) {
  ProxyBypassRules rules;
  rules.ParseFromString("192.168.1.1:33");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("192.168.1.1:33", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://192.168.1.1:33")));

  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://192.168.1.1")));
  EXPECT_FALSE(rules.Matches(GURL("http://sup.192.168.1.1:33")));
}

TEST(ProxyBypassRulesTest, IPV6Address) {
  ProxyBypassRules rules;
  rules.ParseFromString("[3ffe:2a00:100:7031:0:0::1]");
  ASSERT_EQ(1u, rules.rules().size());
  // Note that we canonicalized the IP address.
  EXPECT_EQ("[3ffe:2a00:100:7031::1]", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://[3ffe:2a00:100:7031::1]")));
  EXPECT_TRUE(rules.Matches(GURL("http://[3ffe:2a00:100:7031::1]:33")));

  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://sup.192.168.1.1:33")));
}

TEST(ProxyBypassRulesTest, IPV6AddressWithPort) {
  ProxyBypassRules rules;
  rules.ParseFromString("[3ffe:2a00:100:7031::1]:33");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("[3ffe:2a00:100:7031::1]:33", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://[3ffe:2a00:100:7031::1]:33")));

  EXPECT_FALSE(rules.Matches(GURL("http://[3ffe:2a00:100:7031::1]")));
  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com")));
}

TEST(ProxyBypassRulesTest, HTTPOnly) {
  ProxyBypassRules rules;
  rules.ParseFromString("http://www.google.com");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("http://www.google.com", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com/foo")));
  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com:99")));

  EXPECT_FALSE(rules.Matches(GURL("https://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("ftp://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://foo.www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com.org")));
  EXPECT_FALSE(rules.Matches(GURL("https://www.google.com")));
}

TEST(ProxyBypassRulesTest, HTTPOnlyWithWildcard) {
  ProxyBypassRules rules;
  rules.ParseFromString("http://*www.google.com");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("http://*www.google.com", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com/foo")));
  EXPECT_TRUE(rules.Matches(GURL("http://www.google.com:99")));
  EXPECT_TRUE(rules.Matches(GURL("http://foo.www.google.com")));

  EXPECT_FALSE(rules.Matches(GURL("https://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("ftp://www.google.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://www.google.com.org")));
  EXPECT_FALSE(rules.Matches(GURL("https://www.google.com")));
}

TEST(ProxyBypassRulesTest, UseSuffixMatching) {
  ProxyBypassRules rules;
  rules.ParseFromStringUsingSuffixMatching(
      "foo1.com, .foo2.com, 192.168.1.1, "
      "*foobar.com:80, *.foo, http://baz, <local>");
  ASSERT_EQ(7u, rules.rules().size());
  EXPECT_EQ("*foo1.com", rules.rules()[0]->ToString());
  EXPECT_EQ("*.foo2.com", rules.rules()[1]->ToString());
  EXPECT_EQ("192.168.1.1", rules.rules()[2]->ToString());
  EXPECT_EQ("*foobar.com:80", rules.rules()[3]->ToString());
  EXPECT_EQ("*.foo", rules.rules()[4]->ToString());
  EXPECT_EQ("http://*baz", rules.rules()[5]->ToString());
  EXPECT_EQ("<local>", rules.rules()[6]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://foo1.com")));
  EXPECT_TRUE(rules.Matches(GURL("http://aaafoo1.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://aaafoo1.com.net")));
}

TEST(ProxyBypassRulesTest, MultipleRules) {
  ProxyBypassRules rules;
  rules.ParseFromString(".google.com , .foobar.com:30");
  ASSERT_EQ(2u, rules.rules().size());

  EXPECT_TRUE(rules.Matches(GURL("http://baz.google.com:40")));
  EXPECT_FALSE(rules.Matches(GURL("http://google.com:40")));
  EXPECT_TRUE(rules.Matches(GURL("http://bar.foobar.com:30")));
  EXPECT_FALSE(rules.Matches(GURL("http://bar.foobar.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://bar.foobar.com:33")));
}

TEST(ProxyBypassRulesTest, BadInputs) {
  ProxyBypassRules rules;
  EXPECT_FALSE(rules.AddRuleFromString("://"));
  EXPECT_FALSE(rules.AddRuleFromString("  "));
  EXPECT_FALSE(rules.AddRuleFromString("http://"));
  EXPECT_FALSE(rules.AddRuleFromString("*.foo.com:-34"));
  EXPECT_EQ(0u, rules.rules().size());
}

TEST(ProxyBypassRulesTest, Equals) {
  ProxyBypassRules rules1;
  ProxyBypassRules rules2;

  rules1.ParseFromString("foo1.com, .foo2.com");
  rules2.ParseFromString("foo1.com,.FOo2.com");

  EXPECT_TRUE(rules1.Equals(rules2));
  EXPECT_TRUE(rules2.Equals(rules1));

  rules1.ParseFromString(".foo2.com");
  rules2.ParseFromString("foo1.com,.FOo2.com");

  EXPECT_FALSE(rules1.Equals(rules2));
  EXPECT_FALSE(rules2.Equals(rules1));
}

TEST(ProxyBypassRulesTest, BypassLocalNames) {
  const struct {
    const char* url;
    bool expected_is_local;
  } tests[] = {
    // Single-component hostnames are considered local.
    {"http://localhost/x", true},
    {"http://www", true},

    // IPv4 loopback interface.
    {"http://127.0.0.1/x", true},
    {"http://127.0.0.1:80/x", true},

    // IPv6 loopback interface.
    {"http://[::1]:80/x", true},
    {"http://[0:0::1]:6233/x", true},
    {"http://[0:0:0:0:0:0:0:1]/x", true},

    // Non-local URLs.
    {"http://foo.com/", false},
    {"http://localhost.i/", false},
    {"http://www.google.com/", false},
    {"http://192.168.0.1/", false},

    // Try with different protocols.
    {"ftp://127.0.0.1/x", true},
    {"ftp://foobar.com/x", false},

    // This is a bit of a gray-area, but GURL does not strip trailing dots
    // in host-names, so the following are considered non-local.
    {"http://www./x", false},
    {"http://localhost./x", false},
  };

  ProxyBypassRules rules;
  rules.ParseFromString("<local>");

  for (size_t i = 0; i < arraysize(tests); ++i) {
    SCOPED_TRACE(base::StringPrintf(
        "Test[%d]: %s", static_cast<int>(i), tests[i].url));
    EXPECT_EQ(tests[i].expected_is_local, rules.Matches(GURL(tests[i].url)));
  }
}

TEST(ProxyBypassRulesTest, ParseAndMatchCIDR_IPv4) {
  ProxyBypassRules rules;
  rules.ParseFromString("192.168.1.1/16");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("192.168.1.1/16", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://192.168.1.1")));
  EXPECT_TRUE(rules.Matches(GURL("ftp://192.168.4.4")));
  EXPECT_TRUE(rules.Matches(GURL("https://192.168.0.0:81")));
  EXPECT_TRUE(rules.Matches(GURL("http://[::ffff:192.168.11.11]")));

  EXPECT_FALSE(rules.Matches(GURL("http://foobar.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://192.169.1.1")));
  EXPECT_FALSE(rules.Matches(GURL("http://xxx.192.168.1.1")));
  EXPECT_FALSE(rules.Matches(GURL("http://192.168.1.1.xx")));
}

TEST(ProxyBypassRulesTest, ParseAndMatchCIDR_IPv6) {
  ProxyBypassRules rules;
  rules.ParseFromString("a:b:c:d::/48");
  ASSERT_EQ(1u, rules.rules().size());
  EXPECT_EQ("a:b:c:d::/48", rules.rules()[0]->ToString());

  EXPECT_TRUE(rules.Matches(GURL("http://[A:b:C:9::]")));
  EXPECT_FALSE(rules.Matches(GURL("http://foobar.com")));
  EXPECT_FALSE(rules.Matches(GURL("http://192.169.1.1")));
}

// Check that ProxyBypassRules::MatchesImplicitRules() matches all localhost
// names, and link-local IPs, but nothing else.
TEST(ProxyBypassRulesTest, MatchesImplicitRules) {
  const char* kLocalhosts[] = {
      "localhost",
      "localhost.",
      "foo.localhost",
      "localhost6",
      "localhost6.localdomain6",
      "127.0.0.1",
      "127.100.0.2",
      "[::1]",
  };

  const char* kLinkLocalHosts[] = {
      "169.254.3.2", "169.254.100.1", "[FE80::8]", "[fe91::1]",
  };

  const char* kOtherHosts[] = {
      "192.168.0.1", "170.254.0.0", "128.0.0.1",        "[::2]",
      "[FD80::1]",   "foo",         "www.example3.com", "loopback",
  };

  ExpectMatchesImplicitRules(kLocalhosts, true);
  ExpectMatchesImplicitRules(kLinkLocalHosts, true);
  ExpectMatchesImplicitRules(kOtherHosts, false);
}

}  // namespace

}  // namespace net
