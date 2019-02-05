// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_estimator_util.h"

#include <memory>

#include "base/test/scoped_task_environment.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/dns/host_resolver.h"
#include "net/dns/host_resolver_impl.h"
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

  {
    // Resolve example1.com so that the resolution entry is cached.
    TestCompletionCallback callback;
    std::unique_ptr<HostResolver::Request> request;
    AddressList ignored;
    int rv = mock_host_resolver.Resolve(
        HostResolver::RequestInfo(HostPortPair("example1.com", 443)),
        DEFAULT_PRIORITY, &ignored, callback.callback(), &request,
        NetLogWithSource());
    EXPECT_EQ(ERR_IO_PENDING, rv);
    EXPECT_EQ(OK, callback.WaitForResult());
  }

  {
    // Resolve example2.com so that the resolution entry is cached.
    TestCompletionCallback callback;
    std::unique_ptr<HostResolver::Request> request;
    AddressList ignored;
    int rv = mock_host_resolver.Resolve(
        HostResolver::RequestInfo(HostPortPair("example2.com", 443)),
        DEFAULT_PRIORITY, &ignored, callback.callback(), &request,
        NetLogWithSource());
    EXPECT_EQ(ERR_IO_PENDING, rv);
    EXPECT_EQ(OK, callback.WaitForResult());
  }

  EXPECT_EQ(2u, mock_host_resolver.num_resolve());

  EXPECT_FALSE(IsPrivateHost(&mock_host_resolver,
                             HostPortPair("2607:f8b0:4006:819::200e", 80)));
  EXPECT_EQ(1u, mock_host_resolver.num_resolve_from_cache());

  EXPECT_TRUE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("192.168.0.1", 443)));
  EXPECT_EQ(2u, mock_host_resolver.num_resolve_from_cache());

  EXPECT_FALSE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("92.168.0.1", 443)));
  EXPECT_EQ(3u, mock_host_resolver.num_resolve_from_cache());

  EXPECT_TRUE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("example1.com", 443)));
  EXPECT_EQ(4u, mock_host_resolver.num_resolve_from_cache());

  EXPECT_FALSE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("example2.com", 443)));
  EXPECT_EQ(5u, mock_host_resolver.num_resolve_from_cache());

  // IsPrivateHost() should have queried only the resolver's cache.
  EXPECT_EQ(2u, mock_host_resolver.num_resolve());
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
  EXPECT_EQ(0u, mock_host_resolver.num_resolve());
  EXPECT_EQ(1u, mock_host_resolver.num_resolve_from_cache());

  {
    // Resolve example3.com so that the resolution entry is cached.
    TestCompletionCallback callback;
    std::unique_ptr<HostResolver::Request> request;
    AddressList ignored;
    int rv = mock_host_resolver.Resolve(
        HostResolver::RequestInfo(HostPortPair("example3.com", 443)),
        DEFAULT_PRIORITY, &ignored, callback.callback(), &request,
        NetLogWithSource());
    EXPECT_EQ(ERR_IO_PENDING, rv);
    EXPECT_EQ(OK, callback.WaitForResult());
    EXPECT_EQ(1u, mock_host_resolver.num_resolve());
  }
  EXPECT_TRUE(
      IsPrivateHost(&mock_host_resolver, HostPortPair("example3.com", 443)));

  // IsPrivateHost() should have queried only the resolver's cache.
  EXPECT_EQ(1u, mock_host_resolver.num_resolve());
  EXPECT_EQ(2u, mock_host_resolver.num_resolve_from_cache());
}

// Verify that IsPrivateHost() returns correct results for local hosts.
TEST(NetworkQualityEstimatorUtilTest, Localhost) {
  base::test::ScopedTaskEnvironment scoped_task_environment;

  std::unique_ptr<BoundTestNetLog> net_log =
      std::make_unique<BoundTestNetLog>();
  BoundTestNetLog* net_log_ptr = net_log.get();

  net::HostResolver::Options options;
  // Use HostResolverImpl since MockCachingHostResolver does not determine the
  // correct answer for localhosts.
  HostResolverImpl resolver(options, net_log_ptr->bound().net_log());

  scoped_refptr<net::RuleBasedHostResolverProc> rules(
      new net::RuleBasedHostResolverProc(nullptr));

  EXPECT_TRUE(IsPrivateHost(&resolver, HostPortPair("localhost", 443)));
  EXPECT_TRUE(IsPrivateHost(&resolver, HostPortPair("localhost6", 443)));
  EXPECT_TRUE(IsPrivateHost(&resolver, HostPortPair("127.0.0.1", 80)));
  EXPECT_TRUE(IsPrivateHost(&resolver, HostPortPair("0.0.0.0", 80)));
  EXPECT_TRUE(IsPrivateHost(&resolver, HostPortPair("::1", 80)));
  EXPECT_FALSE(IsPrivateHost(&resolver, HostPortPair("google.com", 80)));
}

}  // namespace

}  // namespace internal

}  // namespace nqe

}  // namespace net
