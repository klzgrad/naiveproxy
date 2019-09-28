// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_estimator_util.h"

#include <memory>

#include "base/optional.h"
#include "base/test/scoped_task_environment.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/dns/context_host_resolver.h"
#include "net/dns/host_resolver.h"
#include "net/dns/mock_host_resolver.h"
#include "net/log/test_net_log.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace net {

namespace nqe {

namespace internal {

namespace {

// Verify that the cached network qualities from the prefs are not used if the
// reading of the network quality prefs is not enabled..
TEST(NetworkQualityEstimatorUtilTest, ReservedHost) {
  base::test::ScopedTaskEnvironment scoped_task_environment;

  std::unique_ptr<BoundTestNetLog> net_log =
      std::make_unique<BoundTestNetLog>();
  MockCachingHostResolver mock_host_resolver;

  scoped_refptr<net::RuleBasedHostResolverProc> rules(
      new net::RuleBasedHostResolverProc(nullptr));

  // example1.com resolves to a private IP address.
  rules->AddRule("example1.com", "127.0.0.3");

  // example2.com resolves to a public IP address.
  rules->AddRule("example2.com", "27.0.0.3");

  mock_host_resolver.set_rules(rules.get());

  EXPECT_EQ(0u, mock_host_resolver.num_resolve());

  // Load hostnames into HostResolver cache.
  int rv = mock_host_resolver.LoadIntoCache(HostPortPair("example1.com", 443),
                                            base::nullopt);
  EXPECT_EQ(OK, rv);
  rv = mock_host_resolver.LoadIntoCache(HostPortPair("example2.com", 443),
                                        base::nullopt);
  EXPECT_EQ(OK, rv);

  EXPECT_EQ(2u, mock_host_resolver.num_non_local_resolves());

  EXPECT_FALSE(IsPrivateHost(&mock_host_resolver,
                             HostPortPair("2607:f8b0:4006:819::200e", 80)));

  EXPECT_TRUE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("192.168.0.1", 443)));

  EXPECT_FALSE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("92.168.0.1", 443)));

  EXPECT_TRUE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("example1.com", 443)));

  EXPECT_FALSE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("example2.com", 443)));

  // IsPrivateHost() should have queried only the resolver's cache.
  EXPECT_EQ(2u, mock_host_resolver.num_non_local_resolves());
}

// Verify that IsPrivateHost() returns false for a hostname whose DNS
// resolution is not cached. Further, once the resolution is cached, verify that
// the cached entry is used.
TEST(NetworkQualityEstimatorUtilTest, ReservedHostUncached) {
  base::test::ScopedTaskEnvironment scoped_task_environment;

  std::unique_ptr<BoundTestNetLog> net_log =
      std::make_unique<BoundTestNetLog>();
  MockCachingHostResolver mock_host_resolver;

  scoped_refptr<net::RuleBasedHostResolverProc> rules(
      new net::RuleBasedHostResolverProc(nullptr));

  // Add example3.com resolution to the DNS cache.
  rules->AddRule("example3.com", "127.0.0.3");
  mock_host_resolver.set_rules(rules.get());

  // Not in DNS host cache, so should not be marked as private.
  EXPECT_FALSE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("example3.com", 443)));
  EXPECT_EQ(0u, mock_host_resolver.num_non_local_resolves());

  int rv = mock_host_resolver.LoadIntoCache(HostPortPair("example3.com", 443),
                                            base::nullopt);
  EXPECT_EQ(OK, rv);
  EXPECT_EQ(1u, mock_host_resolver.num_non_local_resolves());

  EXPECT_TRUE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("example3.com", 443)));

  // IsPrivateHost() should have queried only the resolver's cache.
  EXPECT_EQ(1u, mock_host_resolver.num_non_local_resolves());
}

// Verify that IsPrivateHost() returns correct results for local hosts.
TEST(NetworkQualityEstimatorUtilTest, Localhost) {
  base::test::ScopedTaskEnvironment scoped_task_environment;

  std::unique_ptr<BoundTestNetLog> net_log =
      std::make_unique<BoundTestNetLog>();
  BoundTestNetLog* net_log_ptr = net_log.get();

  // Use actual HostResolver since MockCachingHostResolver does not determine
  // the correct answer for localhosts.
  std::unique_ptr<ContextHostResolver> resolver =
      HostResolver::CreateStandaloneContextResolver(
          net_log_ptr->bound().net_log());

  scoped_refptr<net::RuleBasedHostResolverProc> rules(
      new net::RuleBasedHostResolverProc(nullptr));

  EXPECT_TRUE(IsPrivateHost(resolver.get(), HostPortPair("localhost", 443)));
  EXPECT_TRUE(IsPrivateHost(resolver.get(), HostPortPair("localhost6", 443)));
  EXPECT_TRUE(IsPrivateHost(resolver.get(), HostPortPair("127.0.0.1", 80)));
  EXPECT_TRUE(IsPrivateHost(resolver.get(), HostPortPair("0.0.0.0", 80)));
  EXPECT_TRUE(IsPrivateHost(resolver.get(), HostPortPair("::1", 80)));
  EXPECT_FALSE(IsPrivateHost(resolver.get(), HostPortPair("google.com", 80)));
}

}  // namespace

}  // namespace internal

}  // namespace nqe

}  // namespace net
