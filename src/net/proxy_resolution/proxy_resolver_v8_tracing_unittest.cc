// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/proxy_resolver_v8_tracing.h"

#include <string>
#include <utility>

#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_checker.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/dns/host_cache.h"
#include "net/dns/mock_host_resolver.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/test/event_waiter.h"
#include "net/test/gtest_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

using net::test::IsError;
using net::test::IsOk;

namespace net {

namespace {

class ProxyResolverV8TracingTest : public TestWithScopedTaskEnvironment {
 public:
  void TearDown() override {
    // Drain any pending messages, which may be left over from cancellation.
    // This way they get reliably run as part of the current test, rather than
    // spilling into the next test's execution.
    base::RunLoop().RunUntilIdle();
  }
};

scoped_refptr<PacFileData> LoadScriptData(const char* filename) {
  base::FilePath path;
  base::PathService::Get(base::DIR_SOURCE_ROOT, &path);
  path = path.AppendASCII("net");
  path = path.AppendASCII("data");
  path = path.AppendASCII("proxy_resolver_v8_tracing_unittest");
  path = path.AppendASCII(filename);

  // Try to read the file from disk.
  std::string file_contents;
  bool ok = base::ReadFileToString(path, &file_contents);

  // If we can't load the file from disk, something is misconfigured.
  EXPECT_TRUE(ok) << "Failed to read file: " << path.value();

  // Load the PAC script into the ProxyResolver.
  return PacFileData::FromUTF8(file_contents);
}

class MockBindings {
 public:
  explicit MockBindings(HostResolver* host_resolver)
      : host_resolver_(host_resolver) {}

  void Alert(const base::string16& message) {
    alerts_.push_back(base::UTF16ToASCII(message));
  }
  void OnError(int line_number, const base::string16& error) {
    waiter_.NotifyEvent(EVENT_ERROR);
    errors_.push_back(std::make_pair(line_number, base::UTF16ToASCII(error)));
    if (!error_callback_.is_null())
      error_callback_.Run();
  }

  HostResolver* host_resolver() { return host_resolver_; }

  std::vector<std::string> GetAlerts() {
    return alerts_;
  }

  std::vector<std::pair<int, std::string>> GetErrors() {
    return errors_;
  }

  void RunOnError(const base::Closure& callback) {
    error_callback_ = callback;
    waiter_.WaitForEvent(EVENT_ERROR);
  }

  std::unique_ptr<ProxyResolverV8Tracing::Bindings> CreateBindings() {
    return std::make_unique<ForwardingBindings>(this);
  }

 private:
  class ForwardingBindings : public ProxyResolverV8Tracing::Bindings {
   public:
    explicit ForwardingBindings(MockBindings* bindings) : bindings_(bindings) {}

    // ProxyResolverV8Tracing::Bindings overrides.
    void Alert(const base::string16& message) override {
      DCHECK(thread_checker_.CalledOnValidThread());
      bindings_->Alert(message);
    }

    void OnError(int line_number, const base::string16& error) override {
      DCHECK(thread_checker_.CalledOnValidThread());
      bindings_->OnError(line_number, error);
    }

    NetLogWithSource GetNetLogWithSource() override {
      DCHECK(thread_checker_.CalledOnValidThread());
      return NetLogWithSource();
    }

    HostResolver* GetHostResolver() override {
      DCHECK(thread_checker_.CalledOnValidThread());
      return bindings_->host_resolver();
    }

   private:
    MockBindings* bindings_;
    base::ThreadChecker thread_checker_;
  };

  enum Event {
    EVENT_ERROR,
  };

  std::vector<std::string> alerts_;
  std::vector<std::pair<int, std::string>> errors_;
  HostResolver* const host_resolver_;
  base::Closure error_callback_;
  EventWaiter<Event> waiter_;
};

std::unique_ptr<ProxyResolverV8Tracing> CreateResolver(
    std::unique_ptr<ProxyResolverV8Tracing::Bindings> bindings,
    const char* filename) {
  std::unique_ptr<ProxyResolverV8Tracing> resolver;
  std::unique_ptr<ProxyResolverV8TracingFactory> factory(
      ProxyResolverV8TracingFactory::Create());
  TestCompletionCallback callback;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  factory->CreateProxyResolverV8Tracing(LoadScriptData(filename),
                                        std::move(bindings), &resolver,
                                        callback.callback(), &request);
  EXPECT_THAT(callback.WaitForResult(), IsOk());
  EXPECT_TRUE(resolver);
  return resolver;
}

TEST_F(ProxyResolverV8TracingTest, Simple) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "simple.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;
  std::unique_ptr<ProxyResolver::Request> req;

  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ("foo:99", proxy_info.proxy_server().ToURI());

  EXPECT_EQ(0u, host_resolver.num_resolve());

  // There were no alerts or errors.
  EXPECT_TRUE(mock_bindings.GetAlerts().empty());
  EXPECT_TRUE(mock_bindings.GetErrors().empty());
}

TEST_F(ProxyResolverV8TracingTest, JavascriptError) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "error.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://throw-an-error/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_PAC_SCRIPT_FAILED));

  EXPECT_EQ(0u, host_resolver.num_resolve());

  // Check the output -- there was 1 alert and 1 javascript error.
  ASSERT_EQ(1u, mock_bindings.GetAlerts().size());
  EXPECT_EQ("Prepare to DIE!", mock_bindings.GetAlerts()[0]);
  ASSERT_EQ(1u, mock_bindings.GetErrors().size());
  EXPECT_EQ(5, mock_bindings.GetErrors()[0].first);
  EXPECT_EQ("Uncaught TypeError: Cannot read property 'split' of null",
            mock_bindings.GetErrors()[0].second);
}

TEST_F(ProxyResolverV8TracingTest, TooManyAlerts) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "too_many_alerts.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // Iteration1 does a DNS resolve
  // Iteration2 exceeds the alert buffer
  // Iteration3 runs in blocking mode and completes
  EXPECT_EQ("foo:3", proxy_info.proxy_server().ToURI());

  EXPECT_EQ(1u, host_resolver.num_resolve());

  // No errors.
  EXPECT_TRUE(mock_bindings.GetErrors().empty());

  // Check the alerts -- the script generated 50 alerts.
  std::vector<std::string> alerts = mock_bindings.GetAlerts();
  ASSERT_EQ(50u, alerts.size());
  for (size_t i = 0; i < alerts.size(); i++) {
    EXPECT_EQ("Gee, all these alerts are silly!", alerts[i]);
  }
}

// Verify that buffered alerts cannot grow unboundedly, even when the message is
// empty string.
TEST_F(ProxyResolverV8TracingTest, TooManyEmptyAlerts) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver = CreateResolver(
      mock_bindings.CreateBindings(), "too_many_empty_alerts.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ("foo:3", proxy_info.proxy_server().ToURI());

  EXPECT_EQ(1u, host_resolver.num_resolve());

  // No errors.
  EXPECT_TRUE(mock_bindings.GetErrors().empty());

  // Check the alerts -- the script generated 1000 alerts.
  std::vector<std::string> alerts = mock_bindings.GetAlerts();
  ASSERT_EQ(1000u, alerts.size());
  for (size_t i = 0; i < alerts.size(); i++) {
    EXPECT_EQ("", alerts[i]);
  }
}

// This test runs a PAC script that issues a sequence of DNS resolves. The test
// verifies the final result, and that the underlying DNS resolver received
// the correct set of queries.
TEST_F(ProxyResolverV8TracingTest, Dns) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddRuleForAddressFamily(
      "host1", ADDRESS_FAMILY_IPV4, "166.155.144.44");
  host_resolver.rules()
      ->AddIPLiteralRule("host1", "::1,192.168.1.1", std::string());
  host_resolver.rules()->AddSimulatedFailure("host2");
  host_resolver.rules()->AddRule("host3", "166.155.144.33");
  host_resolver.rules()->AddRule("host5", "166.155.144.55");
  host_resolver.rules()->AddSimulatedFailure("host6");
  host_resolver.rules()->AddRuleForAddressFamily(
      "*", ADDRESS_FAMILY_IPV4, "122.133.144.155");
  host_resolver.rules()->AddRule("*", "133.122.100.200");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "dns.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // The test does 13 DNS resolution, however only 7 of them are unique.
  EXPECT_EQ(7u, host_resolver.num_resolve());

  const char* kExpectedResult =
    "122.133.144.155-"  // myIpAddress()
    "null-"  // dnsResolve('')
    "__1_192.168.1.1-"  // dnsResolveEx('host1')
    "null-"  // dnsResolve('host2')
    "166.155.144.33-"  // dnsResolve('host3')
    "122.133.144.155-"  // myIpAddress()
    "166.155.144.33-"  // dnsResolve('host3')
    "__1_192.168.1.1-"  // dnsResolveEx('host1')
    "122.133.144.155-"  // myIpAddress()
    "null-"  // dnsResolve('host2')
    "-"  // dnsResolveEx('host6')
    "133.122.100.200-"  // myIpAddressEx()
    "166.155.144.44"  // dnsResolve('host1')
    ":99";

  EXPECT_EQ(kExpectedResult, proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_TRUE(mock_bindings.GetErrors().empty());

  // The script generated 1 alert.
  ASSERT_EQ(1u, mock_bindings.GetAlerts().size());
  EXPECT_EQ("iteration: 7", mock_bindings.GetAlerts()[0]);
}

// This test runs a PAC script that does "myIpAddress()" followed by
// "dnsResolve()". This requires 2 restarts. However once the HostResolver's
// cache is warmed, subsequent calls should take 0 restarts.
TEST_F(ProxyResolverV8TracingTest, DnsChecksCache) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddRule("foopy", "166.155.144.11");
  host_resolver.rules()->AddRule("*", "122.133.144.155");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "simple_dns.js");

  TestCompletionCallback callback1;
  TestCompletionCallback callback2;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foopy/req1"), &proxy_info,
                           callback1.callback(), &req,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback1.WaitForResult(), IsOk());

  // The test does 2 DNS resolutions.
  EXPECT_EQ(2u, host_resolver.num_resolve());

  // The first request took 2 restarts, hence on g_iteration=3.
  EXPECT_EQ("166.155.144.11:3", proxy_info.proxy_server().ToURI());

  std::unique_ptr<ProxyResolver::Request> req2;
  resolver->GetProxyForURL(GURL("http://foopy/req2"), &proxy_info,
                           callback2.callback(), &req2,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback2.WaitForResult(), IsOk());

  EXPECT_EQ(4u, host_resolver.num_resolve());

  // This time no restarts were required, so g_iteration incremented by 1.
  EXPECT_EQ("166.155.144.11:4", proxy_info.proxy_server().ToURI());

  // There were no alerts or errors.
  EXPECT_TRUE(mock_bindings.GetAlerts().empty());
  EXPECT_TRUE(mock_bindings.GetErrors().empty());
}

// This test runs a weird PAC script that was designed to defeat the DNS tracing
// optimization. The proxy resolver should detect the inconsistency and
// fall-back to synchronous mode execution.
TEST_F(ProxyResolverV8TracingTest, FallBackToSynchronous1) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddRule("host1", "166.155.144.11");
  host_resolver.rules()->AddRule("crazy4", "133.199.111.4");
  host_resolver.rules()->AddRule("*", "122.133.144.155");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "global_sideffects1.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // The script itself only does 2 DNS resolves per execution, however it
  // constructs the hostname using a global counter which changes on each
  // invocation.
  EXPECT_EQ(3u, host_resolver.num_resolve());

  EXPECT_EQ("166.155.144.11-133.199.111.4:100",
            proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_TRUE(mock_bindings.GetErrors().empty());

  ASSERT_EQ(1u, mock_bindings.GetAlerts().size());
  EXPECT_EQ("iteration: 4", mock_bindings.GetAlerts()[0]);
}

// This test runs a weird PAC script that was designed to defeat the DNS tracing
// optimization. The proxy resolver should detect the inconsistency and
// fall-back to synchronous mode execution.
TEST_F(ProxyResolverV8TracingTest, FallBackToSynchronous2) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddRule("host1", "166.155.144.11");
  host_resolver.rules()->AddRule("host2", "166.155.144.22");
  host_resolver.rules()->AddRule("host3", "166.155.144.33");
  host_resolver.rules()->AddRule("host4", "166.155.144.44");
  host_resolver.rules()->AddRule("*", "122.133.144.155");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "global_sideffects2.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ(3u, host_resolver.num_resolve());

  EXPECT_EQ("166.155.144.44:100", proxy_info.proxy_server().ToURI());

  // There were no alerts or errors.
  EXPECT_TRUE(mock_bindings.GetAlerts().empty());
  EXPECT_TRUE(mock_bindings.GetErrors().empty());
}

// This test runs a weird PAC script that yields a never ending sequence
// of DNS resolves when restarting. Running it will hit the maximum
// DNS resolves per request limit (20) after which every DNS resolve will
// fail.
TEST_F(ProxyResolverV8TracingTest, InfiniteDNSSequence) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddRule("host*", "166.155.144.11");
  host_resolver.rules()->AddRule("*", "122.133.144.155");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "global_sideffects3.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ(20u, host_resolver.num_resolve());

  EXPECT_EQ(
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "null:21", proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_TRUE(mock_bindings.GetErrors().empty());

  // 1 alert.
  EXPECT_EQ(1u, mock_bindings.GetAlerts().size());
  EXPECT_EQ("iteration: 21", mock_bindings.GetAlerts()[0]);
}

// This test runs a weird PAC script that yields a never ending sequence
// of DNS resolves when restarting. Running it will hit the maximum
// DNS resolves per request limit (20) after which every DNS resolve will
// fail.
TEST_F(ProxyResolverV8TracingTest, InfiniteDNSSequence2) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddRule("host*", "166.155.144.11");
  host_resolver.rules()->AddRule("*", "122.133.144.155");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "global_sideffects4.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ(20u, host_resolver.num_resolve());

  EXPECT_EQ("null21:34", proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_TRUE(mock_bindings.GetErrors().empty());

  // 1 alert.
  EXPECT_EQ(1u, mock_bindings.GetAlerts().size());
  EXPECT_EQ("iteration: 21", mock_bindings.GetAlerts()[0]);
}

void DnsDuringInitHelper(bool synchronous_host_resolver) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);
  host_resolver.set_synchronous_mode(synchronous_host_resolver);

  host_resolver.rules()->AddRule("host1", "91.13.12.1");
  host_resolver.rules()->AddRule("host2", "91.13.12.2");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "dns_during_init.js");

  // Initialization did 2 dnsResolves.
  EXPECT_EQ(2u, host_resolver.num_resolve());

  host_resolver.rules()->ClearRules();
  host_resolver.GetHostCache()->clear();

  host_resolver.rules()->AddRule("host1", "145.88.13.3");
  host_resolver.rules()->AddRule("host2", "137.89.8.45");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // Fetched host1 and host2 again, since the ones done during initialization
  // should not have been cached.
  EXPECT_EQ(4u, host_resolver.num_resolve());

  EXPECT_EQ("91.13.12.1-91.13.12.2-145.88.13.3-137.89.8.45:99",
            proxy_info.proxy_server().ToURI());

  // 2 alerts.
  ASSERT_EQ(2u, mock_bindings.GetAlerts().size());
  EXPECT_EQ("Watsup", mock_bindings.GetAlerts()[0]);
  EXPECT_EQ("Watsup2", mock_bindings.GetAlerts()[1]);
}

// Tests a PAC script which does DNS resolves during initialization.
TEST_F(ProxyResolverV8TracingTest, DnsDuringInit) {
  // Test with both both a host resolver that always completes asynchronously,
  // and then again with one that completes synchronously.
  DnsDuringInitHelper(false);
  DnsDuringInitHelper(true);
}

void CrashCallback(int) {
  // Be extra sure that if the callback ever gets invoked, the test will fail.
  CHECK(false);
}

// Start some requests, cancel them all, and then destroy the resolver.
// Note the execution order for this test can vary. Since multiple
// threads are involved, the cancellation may be received a different
// times.
TEST_F(ProxyResolverV8TracingTest, CancelAll) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddSimulatedFailure("*");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "dns.js");

  const size_t kNumRequests = 5;
  ProxyInfo proxy_info[kNumRequests];
  std::unique_ptr<ProxyResolver::Request> request[kNumRequests];

  for (size_t i = 0; i < kNumRequests; ++i) {
    resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info[i],
                             base::Bind(&CrashCallback), &request[i],
                             mock_bindings.CreateBindings());
  }

  for (size_t i = 0; i < kNumRequests; ++i) {
    request[i].reset();
  }
}

// Note the execution order for this test can vary. Since multiple
// threads are involved, the cancellation may be received a different
// times.
TEST_F(ProxyResolverV8TracingTest, CancelSome) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddSimulatedFailure("*");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "dns.js");

  ProxyInfo proxy_info1;
  ProxyInfo proxy_info2;
  std::unique_ptr<ProxyResolver::Request> request1;
  std::unique_ptr<ProxyResolver::Request> request2;
  TestCompletionCallback callback;

  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info1,
                           base::Bind(&CrashCallback), &request1,
                           mock_bindings.CreateBindings());
  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info2,
                           callback.callback(), &request2,
                           mock_bindings.CreateBindings());

  request1.reset();

  EXPECT_THAT(callback.WaitForResult(), IsOk());
}

// Cancel a request after it has finished running on the worker thread, and has
// posted a task the completion task back to origin thread.
TEST_F(ProxyResolverV8TracingTest, CancelWhilePendingCompletionTask) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddSimulatedFailure("*");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "error.js");

  ProxyInfo proxy_info1;
  ProxyInfo proxy_info2;
  std::unique_ptr<ProxyResolver::Request> request1;
  std::unique_ptr<ProxyResolver::Request> request2;
  TestCompletionCallback callback;

  resolver->GetProxyForURL(GURL("http://throw-an-error/"), &proxy_info1,
                           base::Bind(&CrashCallback), &request1,
                           mock_bindings.CreateBindings());

  // Wait until the first request has finished running on the worker thread.
  // Cancel the first request, while it is running its completion task on
  // the origin thread. Reset deletes Request opject which cancels the request.
  mock_bindings.RunOnError(
      base::Bind(&std::unique_ptr<ProxyResolver::Request>::reset,
                 base::Unretained(&request1), nullptr));

  // Start another request, to make sure it is able to complete.
  resolver->GetProxyForURL(GURL("http://i-have-no-idea-what-im-doing/"),
                           &proxy_info2, callback.callback(), &request2,
                           mock_bindings.CreateBindings());

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ("i-approve-this-message:42", proxy_info2.proxy_server().ToURI());
}

// This implementation of HostResolver allows blocking until a resolve request
// has been received. The resolve requests it receives will never be completed.
class BlockableHostResolver : public HostResolver {
 public:
  BlockableHostResolver()
      : num_cancelled_requests_(0), waiting_for_resolve_(false) {}

  std::unique_ptr<ResolveHostRequest> CreateRequest(
      const HostPortPair& host,
      const NetLogWithSource& net_log,
      const base::Optional<ResolveHostParameters>& optional_parameters)
      override {
    NOTIMPLEMENTED();
    return nullptr;
  }

  int Resolve(const RequestInfo& info,
              RequestPriority priority,
              AddressList* addresses,
              CompletionOnceCallback callback,
              std::unique_ptr<Request>* out_req,
              const NetLogWithSource& net_log) override {
    EXPECT_FALSE(callback.is_null());
    EXPECT_TRUE(out_req);

    if (!action_.is_null())
      action_.Run();

    // Indicate to the caller that a request was received.
    EXPECT_TRUE(waiting_for_resolve_);
    base::RunLoop::QuitCurrentWhenIdleDeprecated();

    // This line is intentionally after action_.Run(), since one of the
    // tests does a cancellation inside of Resolve(), and it is more
    // interesting if *out_req hasn't been written yet at that point.
    out_req->reset(new RequestImpl(this));

    // Return ERR_IO_PENDING as this request will NEVER be completed.
    // Expectation is for the caller to later cancel the request.
    return ERR_IO_PENDING;
  }

  int ResolveFromCache(const RequestInfo& info,
                       AddressList* addresses,
                       const NetLogWithSource& net_log) override {
    NOTREACHED();
    return ERR_DNS_CACHE_MISS;
  }

  int ResolveStaleFromCache(const RequestInfo& info,
                            AddressList* addresses,
                            HostCache::EntryStaleness* stale_info,
                            const NetLogWithSource& net_log) override {
    NOTREACHED();
    return ERR_DNS_CACHE_MISS;
  }

  bool HasCached(base::StringPiece hostname,
                 HostCache::Entry::Source* source_out,
                 HostCache::EntryStaleness* stale_out) const override {
    NOTIMPLEMENTED();
    return false;
  }

  void IncreaseNumOfCancelledRequests() { num_cancelled_requests_++; }

  void SetAction(const base::Callback<void(void)>& action) {
    action_ = action;
  }

  // Waits until Resolve() has been called.
  void WaitUntilRequestIsReceived() {
    waiting_for_resolve_ = true;
    base::RunLoop().Run();
    DCHECK(waiting_for_resolve_);
    waiting_for_resolve_ = false;
  }

  int num_cancelled_requests() const {
    return num_cancelled_requests_;
  }

 private:
  class RequestImpl : public HostResolver::Request {
   public:
    RequestImpl(BlockableHostResolver* resolver) : resolver_(resolver) {}

    ~RequestImpl() override {
      if (resolver_)
        resolver_->IncreaseNumOfCancelledRequests();
    }

    void ChangeRequestPriority(RequestPriority priority) override {}

   private:
    BlockableHostResolver* resolver_;

    DISALLOW_COPY_AND_ASSIGN(RequestImpl);
  };

  int num_cancelled_requests_;
  bool waiting_for_resolve_;
  base::Callback<void(void)> action_;
};

// This cancellation test exercises a more predictable cancellation codepath --
// when the request has an outstanding DNS request in flight.
TEST_F(ProxyResolverV8TracingTest, CancelWhileOutstandingNonBlockingDns) {
  BlockableHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "dns.js");

  ProxyInfo proxy_info1;
  ProxyInfo proxy_info2;
  std::unique_ptr<ProxyResolver::Request> request1;
  std::unique_ptr<ProxyResolver::Request> request2;

  resolver->GetProxyForURL(GURL("http://foo/req1"), &proxy_info1,
                           base::Bind(&CrashCallback), &request1,
                           mock_bindings.CreateBindings());

  host_resolver.WaitUntilRequestIsReceived();

  resolver->GetProxyForURL(GURL("http://foo/req2"), &proxy_info2,
                           base::Bind(&CrashCallback), &request2,
                           mock_bindings.CreateBindings());

  host_resolver.WaitUntilRequestIsReceived();

  request1.reset();
  request2.reset();

  EXPECT_EQ(2, host_resolver.num_cancelled_requests());

  // After leaving this scope, the ProxyResolver is destroyed.
  // This should not cause any problems, as the outstanding work
  // should have been cancelled.
}

void CancelRequestAndPause(std::unique_ptr<ProxyResolver::Request>* request) {
  request->reset();

  // Sleep for a little bit. This makes it more likely for the worker
  // thread to have returned from its call, and serves as a regression
  // test for http://crbug.com/173373.
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(30));
}

// In non-blocking mode, the worker thread actually does block for
// a short time to see if the result is in the DNS cache. Test
// cancellation while the worker thread is waiting on this event.
TEST_F(ProxyResolverV8TracingTest, CancelWhileBlockedInNonBlockingDns) {
  BlockableHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "dns.js");

  ProxyInfo proxy_info;
  std::unique_ptr<ProxyResolver::Request> request;

  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           base::Bind(&CrashCallback), &request,
                           mock_bindings.CreateBindings());

  host_resolver.SetAction(base::Bind(CancelRequestAndPause, &request));

  host_resolver.WaitUntilRequestIsReceived();
}

// Cancel the request while there is a pending DNS request, however before
// the request is sent to the host resolver.
TEST_F(ProxyResolverV8TracingTest, CancelWhileBlockedInNonBlockingDns2) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "dns.js");

  ProxyInfo proxy_info;
  std::unique_ptr<ProxyResolver::Request> request;

  resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                           base::Bind(&CrashCallback), &request,
                           mock_bindings.CreateBindings());

  // Wait a bit, so the DNS task has hopefully been posted. The test will
  // work whatever the delay is here, but it is most useful if the delay
  // is large enough to allow a task to be posted back.
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
  request.reset();

  EXPECT_EQ(0u, host_resolver.num_resolve());
}

TEST_F(ProxyResolverV8TracingTest,
       CancelCreateResolverWhileOutstandingBlockingDns) {
  BlockableHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8TracingFactory> factory(
      ProxyResolverV8TracingFactory::Create());
  std::unique_ptr<ProxyResolverV8Tracing> resolver;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  factory->CreateProxyResolverV8Tracing(
      LoadScriptData("dns_during_init.js"), mock_bindings.CreateBindings(),
      &resolver, base::Bind(&CrashCallback), &request);

  host_resolver.WaitUntilRequestIsReceived();

  request.reset();
  EXPECT_EQ(1, host_resolver.num_cancelled_requests());
}

TEST_F(ProxyResolverV8TracingTest, DeleteFactoryWhileOutstandingBlockingDns) {
  BlockableHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8Tracing> resolver;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  {
    std::unique_ptr<ProxyResolverV8TracingFactory> factory(
        ProxyResolverV8TracingFactory::Create());

    factory->CreateProxyResolverV8Tracing(
        LoadScriptData("dns_during_init.js"), mock_bindings.CreateBindings(),
        &resolver, base::Bind(&CrashCallback), &request);
    host_resolver.WaitUntilRequestIsReceived();
  }
  EXPECT_EQ(1, host_resolver.num_cancelled_requests());
}

TEST_F(ProxyResolverV8TracingTest, ErrorLoadingScript) {
  BlockableHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  std::unique_ptr<ProxyResolverV8TracingFactory> factory(
      ProxyResolverV8TracingFactory::Create());
  std::unique_ptr<ProxyResolverV8Tracing> resolver;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  TestCompletionCallback callback;
  factory->CreateProxyResolverV8Tracing(
      LoadScriptData("error_on_load.js"), mock_bindings.CreateBindings(),
      &resolver, callback.callback(), &request);

  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_PAC_SCRIPT_FAILED));
  EXPECT_FALSE(resolver);
}

// This tests that the execution of a PAC script is terminated when the DNS
// dependencies are missing. If the test fails, then it will hang.
TEST_F(ProxyResolverV8TracingTest, Terminate) {
  MockCachingHostResolver host_resolver;
  MockBindings mock_bindings(&host_resolver);

  host_resolver.rules()->AddRule("host1", "182.111.0.222");
  host_resolver.rules()->AddRule("host2", "111.33.44.55");

  std::unique_ptr<ProxyResolverV8Tracing> resolver =
      CreateResolver(mock_bindings.CreateBindings(), "terminate.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  resolver->GetProxyForURL(GURL("http://foopy/req1"), &proxy_info,
                           callback.callback(), &req,
                           mock_bindings.CreateBindings());
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // The test does 2 DNS resolutions.
  EXPECT_EQ(2u, host_resolver.num_resolve());

  EXPECT_EQ("foopy:3", proxy_info.proxy_server().ToURI());

  // No errors or alerts.
  EXPECT_TRUE(mock_bindings.GetErrors().empty());
  EXPECT_TRUE(mock_bindings.GetAlerts().empty());
}

// Tests that multiple instances of ProxyResolverV8Tracing can coexist and run
// correctly at the same time. This is relevant because at the moment (time
// this test was written) each ProxyResolverV8Tracing creates its own thread to
// run V8 on, however each thread is operating on the same v8::Isolate.
TEST_F(ProxyResolverV8TracingTest, MultipleResolvers) {
  // ------------------------
  // Setup resolver0
  // ------------------------
  MockHostResolver host_resolver0;
  MockBindings mock_bindings0(&host_resolver0);
  host_resolver0.rules()->AddRuleForAddressFamily(
      "host1", ADDRESS_FAMILY_IPV4, "166.155.144.44");
  host_resolver0.rules()
      ->AddIPLiteralRule("host1", "::1,192.168.1.1", std::string());
  host_resolver0.rules()->AddSimulatedFailure("host2");
  host_resolver0.rules()->AddRule("host3", "166.155.144.33");
  host_resolver0.rules()->AddRule("host5", "166.155.144.55");
  host_resolver0.rules()->AddSimulatedFailure("host6");
  host_resolver0.rules()->AddRuleForAddressFamily(
      "*", ADDRESS_FAMILY_IPV4, "122.133.144.155");
  host_resolver0.rules()->AddRule("*", "133.122.100.200");
  std::unique_ptr<ProxyResolverV8Tracing> resolver0 =
      CreateResolver(mock_bindings0.CreateBindings(), "dns.js");

  // ------------------------
  // Setup resolver1
  // ------------------------
  std::unique_ptr<ProxyResolverV8Tracing> resolver1 =
      CreateResolver(mock_bindings0.CreateBindings(), "dns.js");

  // ------------------------
  // Setup resolver2
  // ------------------------
  std::unique_ptr<ProxyResolverV8Tracing> resolver2 =
      CreateResolver(mock_bindings0.CreateBindings(), "simple.js");

  // ------------------------
  // Setup resolver3
  // ------------------------
  MockHostResolver host_resolver3;
  MockBindings mock_bindings3(&host_resolver3);
  host_resolver3.rules()->AddRule("foo", "166.155.144.33");
  std::unique_ptr<ProxyResolverV8Tracing> resolver3 =
      CreateResolver(mock_bindings3.CreateBindings(), "simple_dns.js");

  // ------------------------
  // Queue up work for each resolver (which will be running in parallel).
  // ------------------------

  ProxyResolverV8Tracing* resolver[] = {
      resolver0.get(), resolver1.get(), resolver2.get(), resolver3.get(),
  };

  const size_t kNumResolvers = arraysize(resolver);
  const size_t kNumIterations = 20;
  const size_t kNumResults = kNumResolvers * kNumIterations;
  TestCompletionCallback callback[kNumResults];
  ProxyInfo proxy_info[kNumResults];
  std::unique_ptr<ProxyResolver::Request> request[kNumResults];

  for (size_t i = 0; i < kNumResults; ++i) {
    size_t resolver_i = i % kNumResolvers;
    resolver[resolver_i]->GetProxyForURL(
        GURL("http://foo/"), &proxy_info[i], callback[i].callback(),
        &request[i], resolver_i == 3 ? mock_bindings3.CreateBindings()
                                     : mock_bindings0.CreateBindings());
  }

  // ------------------------
  // Verify all of the results.
  // ------------------------

  const char* kExpectedForDnsJs =
    "122.133.144.155-"  // myIpAddress()
    "null-"  // dnsResolve('')
    "__1_192.168.1.1-"  // dnsResolveEx('host1')
    "null-"  // dnsResolve('host2')
    "166.155.144.33-"  // dnsResolve('host3')
    "122.133.144.155-"  // myIpAddress()
    "166.155.144.33-"  // dnsResolve('host3')
    "__1_192.168.1.1-"  // dnsResolveEx('host1')
    "122.133.144.155-"  // myIpAddress()
    "null-"  // dnsResolve('host2')
    "-"  // dnsResolveEx('host6')
    "133.122.100.200-"  // myIpAddressEx()
    "166.155.144.44"  // dnsResolve('host1')
    ":99";

  for (size_t i = 0; i < kNumResults; ++i) {
    size_t resolver_i = i % kNumResolvers;
    EXPECT_THAT(callback[i].WaitForResult(), IsOk());

    std::string proxy_uri = proxy_info[i].proxy_server().ToURI();

    if (resolver_i == 0 || resolver_i == 1) {
      EXPECT_EQ(kExpectedForDnsJs, proxy_uri);
    } else if (resolver_i == 2) {
      EXPECT_EQ("foo:99", proxy_uri);
    } else if (resolver_i == 3) {
      EXPECT_EQ("166.155.144.33:",
                proxy_uri.substr(0, proxy_uri.find(':') + 1));
    } else {
      NOTREACHED();
    }
  }
}

}  // namespace

}  // namespace net
