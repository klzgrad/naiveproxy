// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_common_unittest.h"
#include "net/proxy_resolution/proxy_info.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

void ExpectProxyServerEquals(const char* expectation,
                             const ProxyList& proxy_servers) {
  if (expectation == NULL) {
    EXPECT_TRUE(proxy_servers.IsEmpty());
  } else {
    EXPECT_EQ(expectation, proxy_servers.ToPacString());
  }
}

TEST(ProxyConfigTest, Equals) {
  // Test |ProxyConfig::auto_detect|.

  ProxyConfig config1;
  config1.set_auto_detect(true);

  ProxyConfig config2;
  config2.set_auto_detect(false);

  EXPECT_FALSE(config1.Equals(config2));
  EXPECT_FALSE(config2.Equals(config1));

  config2.set_auto_detect(true);

  EXPECT_TRUE(config1.Equals(config2));
  EXPECT_TRUE(config2.Equals(config1));

  // Test |ProxyConfig::pac_url|.

  config2.set_pac_url(GURL("http://wpad/wpad.dat"));

  EXPECT_FALSE(config1.Equals(config2));
  EXPECT_FALSE(config2.Equals(config1));

  config1.set_pac_url(GURL("http://wpad/wpad.dat"));

  EXPECT_TRUE(config1.Equals(config2));
  EXPECT_TRUE(config2.Equals(config1));

  // Test |ProxyConfig::proxy_rules|.

  config2.proxy_rules().type = ProxyConfig::ProxyRules::Type::PROXY_LIST;
  config2.proxy_rules().single_proxies.SetSingleProxyServer(
      ProxyServer::FromURI("myproxy:80", ProxyServer::SCHEME_HTTP));

  EXPECT_FALSE(config1.Equals(config2));
  EXPECT_FALSE(config2.Equals(config1));

  config1.proxy_rules().type = ProxyConfig::ProxyRules::Type::PROXY_LIST;
  config1.proxy_rules().single_proxies.SetSingleProxyServer(
      ProxyServer::FromURI("myproxy:100", ProxyServer::SCHEME_HTTP));

  EXPECT_FALSE(config1.Equals(config2));
  EXPECT_FALSE(config2.Equals(config1));

  config1.proxy_rules().single_proxies.SetSingleProxyServer(
      ProxyServer::FromURI("myproxy", ProxyServer::SCHEME_HTTP));

  EXPECT_TRUE(config1.Equals(config2));
  EXPECT_TRUE(config2.Equals(config1));

  // Test |ProxyConfig::bypass_rules|.

  config2.proxy_rules().bypass_rules.AddRuleFromString("*.google.com");

  EXPECT_FALSE(config1.Equals(config2));
  EXPECT_FALSE(config2.Equals(config1));

  config1.proxy_rules().bypass_rules.AddRuleFromString("*.google.com");

  EXPECT_TRUE(config1.Equals(config2));
  EXPECT_TRUE(config2.Equals(config1));

  // Test |ProxyConfig::proxy_rules.reverse_bypass|.

  config2.proxy_rules().reverse_bypass = true;

  EXPECT_FALSE(config1.Equals(config2));
  EXPECT_FALSE(config2.Equals(config1));

  config1.proxy_rules().reverse_bypass = true;

  EXPECT_TRUE(config1.Equals(config2));
  EXPECT_TRUE(config2.Equals(config1));
}

TEST(ProxyConfigTest, ParseProxyRules) {
  const struct {
    const char* proxy_rules;

    ProxyConfig::ProxyRules::Type type;
    // These will be PAC-stle strings, eg 'PROXY foo.com'
    const char* single_proxy;
    const char* proxy_for_http;
    const char* proxy_for_https;
    const char* proxy_for_ftp;
    const char* fallback_proxy;
  } tests[] = {
    // One HTTP proxy for all schemes.
    {
      "myproxy:80",

      ProxyConfig::ProxyRules::Type::PROXY_LIST,
      "PROXY myproxy:80",
      NULL,
      NULL,
      NULL,
      NULL,
    },

    // Multiple HTTP proxies for all schemes.
    {
      "myproxy:80,https://myotherproxy",

      ProxyConfig::ProxyRules::Type::PROXY_LIST,
      "PROXY myproxy:80;HTTPS myotherproxy:443",
      NULL,
      NULL,
      NULL,
      NULL,
    },

    // Only specify a proxy server for "http://" urls.
    {
      "http=myproxy:80",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "PROXY myproxy:80",
      NULL,
      NULL,
      NULL,
    },

    // Specify an HTTP proxy for "ftp://" and a SOCKS proxy for "https://" urls.
    {
      "ftp=ftp-proxy ; https=socks4://foopy",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      NULL,
      "SOCKS foopy:1080",
      "PROXY ftp-proxy:80",
      NULL,
    },

    // Give a scheme-specific proxy as well as a non-scheme specific.
    // The first entry "foopy" takes precedance marking this list as
    // Type::PROXY_LIST.
    {
      "foopy ; ftp=ftp-proxy",

      ProxyConfig::ProxyRules::Type::PROXY_LIST,
      "PROXY foopy:80",
      NULL,
      NULL,
      NULL,
      NULL,
    },

    // Give a scheme-specific proxy as well as a non-scheme specific.
    // The first entry "ftp=ftp-proxy" takes precedance marking this list as
    // Type::PROXY_LIST_PER_SCHEME.
    {
      "ftp=ftp-proxy ; foopy",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      NULL,
      NULL,
      "PROXY ftp-proxy:80",
      NULL,
    },

    // Include a list of entries for a single scheme.
    {
      "ftp=ftp1,ftp2,ftp3",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      NULL,
      NULL,
      "PROXY ftp1:80;PROXY ftp2:80;PROXY ftp3:80",
      NULL,
    },

    // Include multiple entries for the same scheme -- they accumulate.
    {
      "http=http1,http2; http=http3",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "PROXY http1:80;PROXY http2:80;PROXY http3:80",
      NULL,
      NULL,
      NULL,
    },

    // Include lists of entries for multiple schemes.
    {
      "ftp=ftp1,ftp2,ftp3 ; http=http1,http2; ",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "PROXY http1:80;PROXY http2:80",
      NULL,
      "PROXY ftp1:80;PROXY ftp2:80;PROXY ftp3:80",
      NULL,
    },

    // Include non-default proxy schemes.
    {
      "http=https://secure_proxy; ftp=socks4://socks_proxy; https=socks://foo",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "HTTPS secure_proxy:443",
      "SOCKS5 foo:1080",
      "SOCKS socks_proxy:1080",
      NULL,
    },

    // Only SOCKS proxy present, others being blank.
    {
      "socks=foopy",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      NULL,
      NULL,
      NULL,
      "SOCKS foopy:1080",
      },

    // SOCKS proxy present along with other proxies too
    {
      "http=httpproxy ; https=httpsproxy ; ftp=ftpproxy ; socks=foopy ",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "PROXY httpproxy:80",
      "PROXY httpsproxy:80",
      "PROXY ftpproxy:80",
      "SOCKS foopy:1080",
    },

    // SOCKS proxy (with modifier) present along with some proxies
    // (FTP being blank)
    {
      "http=httpproxy ; https=httpsproxy ; socks=socks5://foopy ",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "PROXY httpproxy:80",
      "PROXY httpsproxy:80",
      NULL,
      "SOCKS5 foopy:1080",
      },

    // Include unsupported schemes -- they are discarded.
    {
      "crazy=foopy ; foo=bar ; https=myhttpsproxy",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      NULL,
      "PROXY myhttpsproxy:80",
      NULL,
      NULL,
    },

    // direct:// as first option for a scheme.
    {
      "http=direct://,myhttpproxy; https=direct://",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "DIRECT;PROXY myhttpproxy:80",
      "DIRECT",
      NULL,
      NULL,
    },

    // direct:// as a second option for a scheme.
    {
      "http=myhttpproxy,direct://",

      ProxyConfig::ProxyRules::Type::PROXY_LIST_PER_SCHEME,
      NULL,
      "PROXY myhttpproxy:80;DIRECT",
      NULL,
      NULL,
      NULL,
    },

  };

  ProxyConfig config;

  for (size_t i = 0; i < arraysize(tests); ++i) {
    config.proxy_rules().ParseFromString(tests[i].proxy_rules);

    EXPECT_EQ(tests[i].type, config.proxy_rules().type);
    ExpectProxyServerEquals(tests[i].single_proxy,
                            config.proxy_rules().single_proxies);
    ExpectProxyServerEquals(tests[i].proxy_for_http,
                            config.proxy_rules().proxies_for_http);
    ExpectProxyServerEquals(tests[i].proxy_for_https,
                            config.proxy_rules().proxies_for_https);
    ExpectProxyServerEquals(tests[i].proxy_for_ftp,
                            config.proxy_rules().proxies_for_ftp);
    ExpectProxyServerEquals(tests[i].fallback_proxy,
                            config.proxy_rules().fallback_proxies);
  }
}

TEST(ProxyConfigTest, ProxyRulesSetBypassFlag) {
  // Test whether the did_bypass_proxy() flag is set in proxy info correctly.
  ProxyConfig::ProxyRules rules;
  ProxyInfo  result;

  rules.ParseFromString("http=httpproxy:80");
  rules.bypass_rules.AddRuleFromString(".com");

  rules.Apply(GURL("http://example.com"), &result);
  EXPECT_TRUE(result.is_direct_only());
  EXPECT_TRUE(result.did_bypass_proxy());

  rules.Apply(GURL("http://example.org"), &result);
  EXPECT_FALSE(result.is_direct());
  EXPECT_FALSE(result.did_bypass_proxy());

  // Try with reversed bypass rules.
  rules.reverse_bypass = true;

  rules.Apply(GURL("http://example.org"), &result);
  EXPECT_TRUE(result.is_direct_only());
  EXPECT_TRUE(result.did_bypass_proxy());

  rules.Apply(GURL("http://example.com"), &result);
  EXPECT_FALSE(result.is_direct());
  EXPECT_FALSE(result.did_bypass_proxy());
}

static const char kWsUrl[] = "ws://example.com/echo";
static const char kWssUrl[] = "wss://example.com/echo";

class ProxyConfigWebSocketTest : public ::testing::Test {
 protected:
  void ParseFromString(const std::string& rules) {
    rules_.ParseFromString(rules);
  }
  void Apply(const GURL& gurl) { rules_.Apply(gurl, &info_); }
  std::string ToPacString() const { return info_.ToPacString(); }

  static GURL WsUrl() { return GURL(kWsUrl); }
  static GURL WssUrl() { return GURL(kWssUrl); }

  ProxyConfig::ProxyRules rules_;
  ProxyInfo info_;
};

// If a single proxy is set for all protocols, WebSocket uses it.
TEST_F(ProxyConfigWebSocketTest, UsesProxy) {
  ParseFromString("proxy:3128");
  Apply(WsUrl());
  EXPECT_EQ("PROXY proxy:3128", ToPacString());
}

// See RFC6455 Section 4.1. item 3, "_Proxy Usage_".
TEST_F(ProxyConfigWebSocketTest, PrefersSocks) {
  ParseFromString(
      "http=proxy:3128 ; https=sslproxy:3128 ; socks=socksproxy:1080");
  Apply(WsUrl());
  EXPECT_EQ("SOCKS socksproxy:1080", ToPacString());
}

TEST_F(ProxyConfigWebSocketTest, PrefersHttpsToHttp) {
  ParseFromString("http=proxy:3128 ; https=sslproxy:3128");
  Apply(WssUrl());
  EXPECT_EQ("PROXY sslproxy:3128", ToPacString());
}

TEST_F(ProxyConfigWebSocketTest, PrefersHttpsEvenForWs) {
  ParseFromString("http=proxy:3128 ; https=sslproxy:3128");
  Apply(WsUrl());
  EXPECT_EQ("PROXY sslproxy:3128", ToPacString());
}

TEST_F(ProxyConfigWebSocketTest, PrefersHttpToDirect) {
  ParseFromString("http=proxy:3128");
  Apply(WssUrl());
  EXPECT_EQ("PROXY proxy:3128", ToPacString());
}

TEST_F(ProxyConfigWebSocketTest, IgnoresFtpProxy) {
  ParseFromString("ftp=ftpproxy:3128");
  Apply(WssUrl());
  EXPECT_EQ("DIRECT", ToPacString());
}

TEST_F(ProxyConfigWebSocketTest, ObeysBypassRules) {
  ParseFromString("http=proxy:3128 ; https=sslproxy:3128");
  rules_.bypass_rules.AddRuleFromString(".chromium.org");
  Apply(GURL("wss://codereview.chromium.org/feed"));
  EXPECT_EQ("DIRECT", ToPacString());
}

TEST_F(ProxyConfigWebSocketTest, ObeysLocalBypass) {
  ParseFromString("http=proxy:3128 ; https=sslproxy:3128");
  rules_.bypass_rules.AddRuleFromString("<local>");
  Apply(GURL("ws://localhost/feed"));
  EXPECT_EQ("DIRECT", ToPacString());
}

}  // namespace
}  // namespace net
