// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/resolve_context.h"

#include <stdint.h>
#include <memory>
#include <string>
#include <utility>

#include "base/bind.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/mock_network_change_notifier.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/dns_config.h"
#include "net/dns/dns_session.h"
#include "net/dns/dns_socket_pool.h"
#include "net/dns/public/dns_protocol.h"
#include "net/socket/socket_test_util.h"
#include "net/test/test_with_task_environment.h"
#include "net/url_request/url_request_context.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

class ResolveContextTest : public TestWithTaskEnvironment {
 public:
  ResolveContextTest() = default;

  scoped_refptr<DnsSession> CreateDnsSession(const DnsConfig& config) {
    auto null_random_callback =
        base::BindRepeating([](int, int) -> int { IMMEDIATE_CRASH(); });
    std::unique_ptr<DnsSocketPool> dns_socket_pool =
        DnsSocketPool::CreateNull(socket_factory_.get(), null_random_callback);

    return base::MakeRefCounted<DnsSession>(config, std::move(dns_socket_pool),
                                            null_random_callback,
                                            nullptr /* netlog */);
  }

 private:
  std::unique_ptr<MockClientSocketFactory> socket_factory_ =
      std::make_unique<MockClientSocketFactory>();
};

DnsConfig CreateDnsConfig(int num_servers, int num_doh_servers) {
  DnsConfig config;
  for (int i = 0; i < num_servers; ++i) {
    IPEndPoint dns_endpoint(IPAddress(192, 168, 1, static_cast<uint8_t>(i)),
                            dns_protocol::kDefaultPort);
    config.nameservers.push_back(dns_endpoint);
  }
  for (int i = 0; i < num_doh_servers; ++i) {
    std::string server_template(
        base::StringPrintf("https://mock.http/doh_test_%d{?dns}", i));
    config.dns_over_https_servers.push_back(DnsConfig::DnsOverHttpsServerConfig(
        server_template, true /* is_post */));
  }

  return config;
}

TEST_F(ResolveContextTest, NoCurrentSession) {
  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);

  context.SetProbeSuccess(1u, true, session.get());

  EXPECT_EQ(base::nullopt,
            context.DohServerIndexToUse(0, DnsConfig::SecureDnsMode::AUTOMATIC,
                                        session.get()));
  EXPECT_EQ(0u, context.NumAvailableDohServers(session.get()));
  EXPECT_FALSE(context.GetDohServerAvailability(1, session.get()));
}

TEST_F(ResolveContextTest, DifferentSession) {
  DnsConfig config1 =
      CreateDnsConfig(1 /* num_servers */, 3 /* num_doh_servers */);
  scoped_refptr<DnsSession> session1 = CreateDnsSession(config1);

  DnsConfig config2 =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session2 = CreateDnsSession(config2);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session2.get());

  // Use current session to set a probe result.
  context.SetProbeSuccess(1u, true, session2.get());

  EXPECT_EQ(base::nullopt,
            context.DohServerIndexToUse(0, DnsConfig::SecureDnsMode::AUTOMATIC,
                                        session1.get()));
  EXPECT_EQ(0u, context.NumAvailableDohServers(session1.get()));
  EXPECT_FALSE(context.GetDohServerAvailability(1u, session1.get()));

  // Different session for SetProbeResult should have no effect.
  ASSERT_TRUE(context.GetDohServerAvailability(1u, session2.get()));
  context.SetProbeSuccess(1u, false, session1.get());
  EXPECT_TRUE(context.GetDohServerAvailability(1u, session2.get()));
}

TEST_F(ResolveContextTest, DohServerAvailability_InitialAvailability) {
  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session.get());

  EXPECT_EQ(context.NumAvailableDohServers(session.get()), 0u);
  EXPECT_EQ(base::nullopt,
            context.DohServerIndexToUse(0u, DnsConfig::SecureDnsMode::AUTOMATIC,
                                        session.get()));
}

TEST_F(ResolveContextTest, DohServerAvailability_ProbeSuccess) {
  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session.get());

  ASSERT_EQ(context.NumAvailableDohServers(session.get()), 0u);

  context.SetProbeSuccess(1, true /* success */, session.get());
  EXPECT_EQ(context.NumAvailableDohServers(session.get()), 1u);
  EXPECT_THAT(context.DohServerIndexToUse(
                  0u, DnsConfig::SecureDnsMode::AUTOMATIC, session.get()),
              testing::Optional(1u));
}

TEST_F(ResolveContextTest, DohServerAvailability_ProbeFailure) {
  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session.get());

  context.SetProbeSuccess(1, true /* success */, session.get());
  ASSERT_EQ(context.NumAvailableDohServers(session.get()), 1u);

  context.SetProbeSuccess(1, false /* success */, session.get());
  EXPECT_EQ(context.NumAvailableDohServers(session.get()), 0u);
  EXPECT_EQ(base::nullopt,
            context.DohServerIndexToUse(0u, DnsConfig::SecureDnsMode::AUTOMATIC,
                                        session.get()));
}

TEST_F(ResolveContextTest, DohServerIndexToUse) {
  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session.get());

  context.SetProbeSuccess(0u, true /* success */, session.get());
  EXPECT_THAT(context.DohServerIndexToUse(
                  0u, DnsConfig::SecureDnsMode::AUTOMATIC, session.get()),
              testing::Optional(0u));
  EXPECT_THAT(context.DohServerIndexToUse(
                  1u, DnsConfig::SecureDnsMode::AUTOMATIC, session.get()),
              testing::Optional(0u));
}

TEST_F(ResolveContextTest, DohServerIndexToUse_NoneEligible) {
  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session.get());

  EXPECT_EQ(base::nullopt,
            context.DohServerIndexToUse(0u, DnsConfig::SecureDnsMode::AUTOMATIC,
                                        session.get()));
  EXPECT_EQ(base::nullopt,
            context.DohServerIndexToUse(1u, DnsConfig::SecureDnsMode::AUTOMATIC,
                                        session.get()));
}

TEST_F(ResolveContextTest, DohServerIndexToUse_SecureMode) {
  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session.get());

  EXPECT_THAT(context.DohServerIndexToUse(0u, DnsConfig::SecureDnsMode::SECURE,
                                          session.get()),
              testing::Optional(0u));
  EXPECT_THAT(context.DohServerIndexToUse(1u, DnsConfig::SecureDnsMode::SECURE,
                                          session.get()),
              testing::Optional(1u));
}

class TestDnsObserver : public NetworkChangeNotifier::DNSObserver {
 public:
  void OnDNSChanged() override { ++dns_changed_calls_; }

  int dns_changed_calls() const { return dns_changed_calls_; }

 private:
  int dns_changed_calls_ = 0;
};

TEST_F(ResolveContextTest, DohServerAvailabilityNotification) {
  test::ScopedMockNetworkChangeNotifier mock_network_change_notifier;
  TestDnsObserver config_observer;
  NetworkChangeNotifier::AddDNSObserver(&config_observer);

  DnsConfig config =
      CreateDnsConfig(2 /* num_servers */, 2 /* num_doh_servers */);
  scoped_refptr<DnsSession> session = CreateDnsSession(config);

  URLRequestContext request_context;
  ResolveContext context(&request_context);
  context.SetCurrentSession(session.get());

  base::RunLoop().RunUntilIdle();  // Notifications are async.
  EXPECT_EQ(0, config_observer.dns_changed_calls());

  // Expect notification on first available DoH server.
  context.SetProbeSuccess(0, true /* success */, session.get());
  base::RunLoop().RunUntilIdle();  // Notifications are async.
  EXPECT_EQ(1, config_observer.dns_changed_calls());

  // No notifications as additional servers are available or unavailable.
  context.SetProbeSuccess(1, true /* success */, session.get());
  context.SetProbeSuccess(0, false /* success */, session.get());
  base::RunLoop().RunUntilIdle();  // Notifications are async.
  EXPECT_EQ(1, config_observer.dns_changed_calls());

  // Expect notification on last server unavailable.
  context.SetProbeSuccess(1, false /* success */, session.get());
  base::RunLoop().RunUntilIdle();  // Notifications are async.
  EXPECT_EQ(2, config_observer.dns_changed_calls());

  NetworkChangeNotifier::RemoveDNSObserver(&config_observer);
}

}  // namespace
}  // namespace net
