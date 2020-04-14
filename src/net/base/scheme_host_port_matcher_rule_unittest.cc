// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/scheme_host_port_matcher_rule.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherHostnamePatternRule_HostOnlyRule) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
          "", "www.google.com", -1);

  EXPECT_EQ("www.google.com", rule->ToString());

  // non-hostname components don't matter.
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://www.google.com:81")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("ftp://www.google.com:99")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com/x/y?q#h")));

  // Hostname must match.
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://foo.www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://xxx.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com.baz.org")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherHostnamePatternRule_BasicDomain) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
          "", "*.google.com", -1);

  EXPECT_EQ("*.google.com", rule->ToString());

  // non-hostname components don't matter.
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://a.google.com:81")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("ftp://www.google.com:99")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://foo.google.com/x/y?q")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://foo:bar@baz.google.com#x")));

  // Hostname must be a strict "ends with" match.
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com.baz.org")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherHostnamePatternRule_BasicDomainWithPort) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
          "", "*.google.com", 80);

  EXPECT_EQ("*.google.com:80", rule->ToString());

  // non-hostname components don't matter.
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://a.google.com:80?x")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://a.google.com:80/x/y?q#f")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("ftp://www.google.com:80")));

  // Hostname must be a strict "ends with" match.
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com.baz.org")));

  // Port must match.
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com:90")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("https://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("https://ftp.google.com")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherHostnamePatternRule_MatchAll) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherHostnamePatternRule>("", "*", -1);

  EXPECT_EQ("*", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("ftp://www.foobar.com:99")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://a.google.com:80/?x")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherHostnamePatternRule_HttpScheme) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
          "http", "www.google.com", -1);

  EXPECT_EQ("http://www.google.com", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com/foo")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com:99")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://foo.www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("https://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("ftp://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com.org")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherHostnamePatternRule_HttpOnlyWithWildcard) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
          "http", "*www.google.com", -1);

  EXPECT_EQ("http://*www.google.com", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com/foo")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.google.com:99")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://foo.www.google.com")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("https://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("ftp://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com.org")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherHostnamePatternRule_PunnyCodeHostname) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherHostnamePatternRule>(
          "", "*.xn--flw351e.cn", -1);

  EXPECT_EQ("*.xn--flw351e.cn", rule->ToString());
  // Google Chinese site.
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://www.谷歌.cn")));
}

TEST(SchemeHostPortMatcherRuleTest, SuffixMatchingTest) {
  // foo1.com, suffix matching rule will match www.foo1.com but the original one
  // doesn't.
  SchemeHostPortMatcherHostnamePatternRule rule1("", "foo1.com", -1);
  std::unique_ptr<SchemeHostPortMatcherHostnamePatternRule>
      suffix_matching_rule = rule1.GenerateSuffixMatchingRule();
  EXPECT_EQ("foo1.com", rule1.ToString());
  EXPECT_EQ("*foo1.com", suffix_matching_rule->ToString());
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule1.Evaluate(GURL("http://www.foo1.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            suffix_matching_rule->Evaluate(GURL("http://www.foo1.com")));

  // .foo2.com, suffix matching rule will match www.foo2.com but the original
  // one doesn't.
  SchemeHostPortMatcherHostnamePatternRule rule2("", ".foo2.com", -1);
  suffix_matching_rule = rule2.GenerateSuffixMatchingRule();
  EXPECT_EQ(".foo2.com", rule2.ToString());
  EXPECT_EQ("*.foo2.com", suffix_matching_rule->ToString());
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule2.Evaluate(GURL("http://www.foo2.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            suffix_matching_rule->Evaluate(GURL("http://www.foo2.com")));

  // *foobar.com:80, this is already a suffix matching rule.
  SchemeHostPortMatcherHostnamePatternRule rule3("", "*foobar.com", 80);
  suffix_matching_rule = rule3.GenerateSuffixMatchingRule();
  EXPECT_EQ("*foobar.com:80", rule3.ToString());
  EXPECT_EQ("*foobar.com:80", suffix_matching_rule->ToString());
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule3.Evaluate(GURL("http://www.foobar.com:80")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            suffix_matching_rule->Evaluate(GURL("http://www.foobar.com:80")));

  // *.foo, this is already a suffix matching rule.
  SchemeHostPortMatcherHostnamePatternRule rule4("", "*.foo", -1);
  suffix_matching_rule = rule4.GenerateSuffixMatchingRule();
  EXPECT_EQ("*.foo", rule4.ToString());
  EXPECT_EQ("*.foo", suffix_matching_rule->ToString());
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule4.Evaluate(GURL("http://www.foo")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            suffix_matching_rule->Evaluate(GURL("http://www.foo")));

  // http://baz, suffix matching works for host part only.
  SchemeHostPortMatcherHostnamePatternRule rule5("http", "baz", -1);
  suffix_matching_rule = rule5.GenerateSuffixMatchingRule();
  EXPECT_EQ("http://baz", rule5.ToString());
  EXPECT_EQ("http://*baz", suffix_matching_rule->ToString());
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule5.Evaluate(GURL("http://foobaz")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            suffix_matching_rule->Evaluate(GURL("http://foobaz")));
}

TEST(SchemeHostPortMatcherRuleTest, SchemeHostPortMatcherIPHostRule_IPv4) {
  IPAddress ip_address;
  ignore_result(ip_address.AssignFromIPLiteral("192.168.1.1"));
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPHostRule>(
          "", IPEndPoint(ip_address, 0));

  EXPECT_EQ("192.168.1.1", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1:90")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1:90/x/y?q")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1:90/x/y?q#h")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://sup.192.168.1.1")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherIPHostRule_IPv4WithPort) {
  IPAddress ip_address;
  ignore_result(ip_address.AssignFromIPLiteral("192.168.1.1"));
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPHostRule>(
          "", IPEndPoint(ip_address, 33));

  EXPECT_EQ("192.168.1.1:33", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1:33")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1:33/x/y?q#h")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1:90")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://sup.192.168.1.1")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherIPHostRule_IPv4WithScheme) {
  IPAddress ip_address;
  ignore_result(ip_address.AssignFromIPLiteral("192.168.1.1"));
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPHostRule>(
          "http", IPEndPoint(ip_address, 0));

  EXPECT_EQ("http://192.168.1.1", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1:90")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1:90/x/y?q#h")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("ftp://192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("https://192.168.1.1")));
}

TEST(SchemeHostPortMatcherRuleTest, SchemeHostPortMatcherIPHostRule_IPv6) {
  IPAddress ip_address;
  // Note that the IPv6 address needs to be canonicalized.
  ignore_result(ip_address.AssignFromIPLiteral("3ffe:2a00:100:7031::1"));
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPHostRule>(
          "", IPEndPoint(ip_address, 0));

  EXPECT_EQ("[3ffe:2a00:100:7031::1]", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[3ffe:2a00:100:7031::1]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[3ffe:2a00:100:7031::1]:33")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[3ffe:2a00:100:7031::1]:33/x/y?q#h")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1:90")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://sup.192.168.1.1")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherIPHostRule_IPv6WithPort) {
  IPAddress ip_address;
  // Note that the IPv6 address needs to be canonicalized.
  ignore_result(ip_address.AssignFromIPLiteral("3ffe:2a00:100:7031::1"));
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPHostRule>(
          "", IPEndPoint(ip_address, 33));

  EXPECT_EQ("[3ffe:2a00:100:7031::1]:33", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[3ffe:2a00:100:7031::1]:33")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[3ffe:2a00:100:7031::1]:33/x/y?q#h")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://[3ffe:2a00:100:7031::1]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1:90")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://www.google.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://sup.192.168.1.1")));
}

TEST(SchemeHostPortMatcherRuleTest,
     SchemeHostPortMatcherIPHostRule_IPv6WithScheme) {
  IPAddress ip_address;
  // Note that the IPv6 address needs to be canonicalized.
  ignore_result(ip_address.AssignFromIPLiteral("3ffe:2a00:100:7031::1"));
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPHostRule>(
          "https", IPEndPoint(ip_address, 0));

  EXPECT_EQ("https://[3ffe:2a00:100:7031::1]", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://[3ffe:2a00:100:7031::1]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://[3ffe:2a00:100:7031::1]:33")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("https://[3ffe:2a00:100:7031::1]:33/x/y?q#h")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://[3ffe:2a00:100:7031::1]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("ftp://[3ffe:2a00:100:7031::1]")));
}

TEST(SchemeHostPortMatcherRuleTest, SchemeHostPortMatcherIPBlockRule_IPv4) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPBlockRule>(
          "192.168.1.1/16", "", IPAddress(192, 168, 1, 1), 16);

  EXPECT_EQ("192.168.1.1/16", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.4.4")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.0.0:81")));
  // Test that an IPv4 mapped IPv6 literal matches an IPv4 CIDR rule.
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[::ffff:192.168.11.11]")));

  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://foobar.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.169.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://xxx.192.168.1.1")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1.xx")));
}

TEST(SchemeHostPortMatcherRuleTest, SchemeHostPortMatcherIPBlockRule_IPv6) {
  std::unique_ptr<SchemeHostPortMatcherRule> rule =
      std::make_unique<SchemeHostPortMatcherIPBlockRule>(
          "a:b:c:d::/48", "",
          IPAddress(0x0, 0xa, 0x0, 0xb, 0x0, 0xc, 0x0, 0xd, 0x0, 0x0, 0x0, 0x0,
                    0x0, 0x0, 0x0, 0x0),
          48);

  EXPECT_EQ("a:b:c:d::/48", rule->ToString());

  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[A:b:C:9::]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://foobar.com")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.1.1")));

  // Test that an IPv4 literal matches an IPv4 mapped IPv6 CIDR rule.
  // This is the IPv4 mapped equivalent to 192.168.1.1/16.
  rule = std::make_unique<SchemeHostPortMatcherIPBlockRule>(
      "::ffff:192.168.1.1/112", "",
      IPAddress(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xff,
                192, 168, 1, 1),
      112);
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[::ffff:192.168.1.3]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://192.168.11.11")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://10.10.1.1")));

  // Test using an IP range that is close to IPv4 mapped, but not
  // quite. Should not result in matches.
  rule = std::make_unique<SchemeHostPortMatcherIPBlockRule>(
      "::fffe:192.168.1.1/112", "",
      IPAddress(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xff, 0xfe,
                192, 168, 1, 1),
      112);
  EXPECT_EQ(SchemeHostPortMatcherResult::kInclude,
            rule->Evaluate(GURL("http://[::fffe:192.168.1.3]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://[::ffff:192.168.1.3]")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://192.168.11.11")));
  EXPECT_EQ(SchemeHostPortMatcherResult::kNoMatch,
            rule->Evaluate(GURL("http://10.10.1.1")));
}

}  // anonymous namespace

}  // namespace net
