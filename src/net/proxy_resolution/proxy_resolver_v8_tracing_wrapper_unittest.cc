// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy_resolution/proxy_resolver_v8_tracing_wrapper.h"

#include <string>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/memory/ptr_util.h"
#include "base/path_service.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/platform_thread.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/base/network_interfaces.h"
#include "net/base/test_completion_callback.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_with_source.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_entry.h"
#include "net/log/test_net_log_util.h"
#include "net/proxy_resolution/mock_proxy_host_resolver.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/proxy_resolution/proxy_resolve_dns_operation.h"
#include "net/proxy_resolution/proxy_resolver_error_observer.h"
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

class ProxyResolverV8TracingWrapperTest : public TestWithScopedTaskEnvironment {
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

std::unique_ptr<ProxyResolverErrorObserver> ReturnErrorObserver(
    std::unique_ptr<ProxyResolverErrorObserver> error_observer) {
  return error_observer;
}

std::unique_ptr<ProxyResolver> CreateResolver(
    NetLog* net_log,
    ProxyHostResolver* host_resolver,
    std::unique_ptr<ProxyResolverErrorObserver> error_observer,
    const char* filename) {
  std::unique_ptr<ProxyResolver> resolver;
  ProxyResolverFactoryV8TracingWrapper factory(
      host_resolver, net_log,
      base::Bind(&ReturnErrorObserver, base::Passed(&error_observer)));
  TestCompletionCallback callback;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  int rv = factory.CreateProxyResolver(LoadScriptData(filename), &resolver,
                                       callback.callback(), &request);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());
  EXPECT_TRUE(resolver);
  return resolver;
}

class MockErrorObserver : public ProxyResolverErrorObserver {
 public:
  void OnPACScriptError(int line_number, const base::string16& error) override {
    output += base::StringPrintf("Error: line %d: %s\n", line_number,
                                 base::UTF16ToASCII(error).c_str());
    waiter_.NotifyEvent(EVENT_ERROR);
    if (!error_callback_.is_null())
      error_callback_.Run();
  }

  std::string GetOutput() {
    return output;
  }

  void RunOnError(const base::Closure& callback) {
    error_callback_ = callback;
    waiter_.WaitForEvent(EVENT_ERROR);
  }

 private:
  enum Event {
    EVENT_ERROR,
  };
  std::string output;

  base::Closure error_callback_;
  EventWaiter<Event> waiter_;
};

TEST_F(ProxyResolverV8TracingWrapperTest, Simple) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      &log, &host_resolver, base::WrapUnique(error_observer), "simple.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ("foo:99", proxy_info.proxy_server().ToURI());

  EXPECT_EQ(0u, host_resolver.num_resolve());

  // There were no errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- nothing was logged.
  EXPECT_EQ(0u, log.GetSize());
  EXPECT_EQ(0u, request_log.GetSize());
}

TEST_F(ProxyResolverV8TracingWrapperTest, JavascriptError) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      &log, &host_resolver, base::WrapUnique(error_observer), "error.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://throw-an-error/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_PAC_SCRIPT_FAILED));

  EXPECT_EQ(0u, host_resolver.num_resolve());

  EXPECT_EQ(
      "Error: line 5: Uncaught TypeError: Cannot read property 'split' "
      "of null\n",
      error_observer->GetOutput());

  // Check the NetLogs -- there was 1 alert and 1 javascript error, and they
  // were output to both the global log, and per-request log.
  TestNetLogEntry::List entries_list[2];
  log.GetEntries(&entries_list[0]);
  request_log.GetEntries(&entries_list[1]);

  for (size_t list_i = 0; list_i < base::size(entries_list); list_i++) {
    const TestNetLogEntry::List& entries = entries_list[list_i];
    EXPECT_EQ(2u, entries.size());
    EXPECT_TRUE(LogContainsEvent(entries, 0,
                                 NetLogEventType::PAC_JAVASCRIPT_ALERT,
                                 NetLogEventPhase::NONE));
    EXPECT_TRUE(LogContainsEvent(entries, 1,
                                 NetLogEventType::PAC_JAVASCRIPT_ERROR,
                                 NetLogEventPhase::NONE));

    EXPECT_EQ("{\"message\":\"Prepare to DIE!\"}", entries[0].GetParamsJson());
    EXPECT_EQ(
        "{\"line_number\":5,\"message\":\"Uncaught TypeError: Cannot "
        "read property 'split' of null\"}",
        entries[1].GetParamsJson());
  }
}

TEST_F(ProxyResolverV8TracingWrapperTest, TooManyAlerts) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver =
      CreateResolver(&log, &host_resolver, base::WrapUnique(error_observer),
                     "too_many_alerts.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // Iteration1 does a DNS resolve
  // Iteration2 exceeds the alert buffer
  // Iteration3 runs in blocking mode and completes
  EXPECT_EQ("foo:3", proxy_info.proxy_server().ToURI());

  EXPECT_EQ(1u, host_resolver.num_resolve());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- the script generated 50 alerts, which were mirrored
  // to both the global and per-request logs.
  TestNetLogEntry::List entries_list[2];
  log.GetEntries(&entries_list[0]);
  request_log.GetEntries(&entries_list[1]);

  for (size_t list_i = 0; list_i < base::size(entries_list); list_i++) {
    const TestNetLogEntry::List& entries = entries_list[list_i];
    EXPECT_EQ(50u, entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
      ASSERT_TRUE(LogContainsEvent(entries, i,
                                   NetLogEventType::PAC_JAVASCRIPT_ALERT,
                                   NetLogEventPhase::NONE));
    }
  }
}

// Verify that buffered alerts cannot grow unboundedly, even when the message is
// empty string.
TEST_F(ProxyResolverV8TracingWrapperTest, TooManyEmptyAlerts) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver =
      CreateResolver(&log, &host_resolver, base::WrapUnique(error_observer),
                     "too_many_empty_alerts.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ("foo:3", proxy_info.proxy_server().ToURI());

  EXPECT_EQ(1u, host_resolver.num_resolve());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- the script generated 50 alerts, which were mirrored
  // to both the global and per-request logs.
  TestNetLogEntry::List entries_list[2];
  log.GetEntries(&entries_list[0]);
  request_log.GetEntries(&entries_list[1]);

  for (size_t list_i = 0; list_i < base::size(entries_list); list_i++) {
    const TestNetLogEntry::List& entries = entries_list[list_i];
    EXPECT_EQ(1000u, entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
      ASSERT_TRUE(LogContainsEvent(entries, i,
                                   NetLogEventType::PAC_JAVASCRIPT_ALERT,
                                   NetLogEventPhase::NONE));
    }
  }
}

// This test runs a PAC script that issues a sequence of DNS resolves. The test
// verifies the final result, and that the underlying DNS resolver received
// the correct set of queries.
TEST_F(ProxyResolverV8TracingWrapperTest, Dns) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.SetResult(GetHostName(),
                          ProxyResolveDnsOperation::MY_IP_ADDRESS,
                          {IPAddress(122, 133, 144, 155)});
  host_resolver.SetResult(GetHostName(),
                          ProxyResolveDnsOperation::MY_IP_ADDRESS_EX,
                          {IPAddress(133, 122, 100, 200)});
  host_resolver.SetError("", ProxyResolveDnsOperation::DNS_RESOLVE);
  host_resolver.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(166, 155, 144, 44)});
  IPAddress v6_local;
  ASSERT_TRUE(v6_local.AssignFromIPLiteral("::1"));
  host_resolver.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE_EX,
                          {v6_local, IPAddress(192, 168, 1, 1)});
  host_resolver.SetError("host2", ProxyResolveDnsOperation::DNS_RESOLVE);
  host_resolver.SetResult("host3", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(166, 155, 144, 33)});
  host_resolver.SetError("host6", ProxyResolveDnsOperation::DNS_RESOLVE_EX);

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      &log, &host_resolver, base::WrapUnique(error_observer), "dns.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // The test does 13 DNS resolution, however only 7 of them are unique.
  EXPECT_EQ(7u, host_resolver.num_resolve());

  const char* kExpectedResult =
      "122.133.144.155-"  // myIpAddress()
      "null-"             // dnsResolve('')
      "__1_192.168.1.1-"  // dnsResolveEx('host1')
      "null-"             // dnsResolve('host2')
      "166.155.144.33-"   // dnsResolve('host3')
      "122.133.144.155-"  // myIpAddress()
      "166.155.144.33-"   // dnsResolve('host3')
      "__1_192.168.1.1-"  // dnsResolveEx('host1')
      "122.133.144.155-"  // myIpAddress()
      "null-"             // dnsResolve('host2')
      "-"                 // dnsResolveEx('host6')
      "133.122.100.200-"  // myIpAddressEx()
      "166.155.144.44"    // dnsResolve('host1')
      ":99";

  EXPECT_EQ(kExpectedResult, proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- the script generated 1 alert, mirrored to both
  // the per-request and global logs.
  TestNetLogEntry::List entries_list[2];
  log.GetEntries(&entries_list[0]);
  request_log.GetEntries(&entries_list[1]);

  for (size_t list_i = 0; list_i < base::size(entries_list); list_i++) {
    const TestNetLogEntry::List& entries = entries_list[list_i];
    EXPECT_EQ(1u, entries.size());
    EXPECT_TRUE(LogContainsEvent(entries, 0,
                                 NetLogEventType::PAC_JAVASCRIPT_ALERT,
                                 NetLogEventPhase::NONE));
    EXPECT_EQ("{\"message\":\"iteration: 7\"}", entries[0].GetParamsJson());
  }
}

// This test runs a weird PAC script that was designed to defeat the DNS tracing
// optimization. The proxy resolver should detect the inconsistency and
// fall-back to synchronous mode execution.
TEST_F(ProxyResolverV8TracingWrapperTest, FallBackToSynchronous1) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(166, 155, 144, 11)});
  host_resolver.SetResult("crazy4", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(133, 199, 111, 4)});

  std::unique_ptr<ProxyResolver> resolver =
      CreateResolver(&log, &host_resolver, base::WrapUnique(error_observer),
                     "global_sideffects1.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // The script itself only does 2 DNS resolves per execution, however it
  // constructs the hostname using a global counter which changes on each
  // invocation.
  EXPECT_EQ(3u, host_resolver.num_resolve());

  EXPECT_EQ("166.155.144.11-133.199.111.4:100",
            proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- the script generated 1 alert, mirrored to both
  // the per-request and global logs.
  TestNetLogEntry::List entries_list[2];
  log.GetEntries(&entries_list[0]);
  request_log.GetEntries(&entries_list[1]);

  for (size_t list_i = 0; list_i < base::size(entries_list); list_i++) {
    const TestNetLogEntry::List& entries = entries_list[list_i];
    EXPECT_EQ(1u, entries.size());
    EXPECT_TRUE(LogContainsEvent(entries, 0,
                                 NetLogEventType::PAC_JAVASCRIPT_ALERT,
                                 NetLogEventPhase::NONE));
    EXPECT_EQ("{\"message\":\"iteration: 4\"}", entries[0].GetParamsJson());
  }
}

// This test runs a weird PAC script that was designed to defeat the DNS tracing
// optimization. The proxy resolver should detect the inconsistency and
// fall-back to synchronous mode execution.
TEST_F(ProxyResolverV8TracingWrapperTest, FallBackToSynchronous2) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(166, 155, 144, 11)});
  host_resolver.SetResult("host2", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(166, 155, 144, 22)});
  host_resolver.SetResult("host3", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(166, 155, 144, 33)});
  host_resolver.SetResult("host4", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(166, 155, 144, 44)});

  std::unique_ptr<ProxyResolver> resolver =
      CreateResolver(&log, &host_resolver, base::WrapUnique(error_observer),
                     "global_sideffects2.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ(3u, host_resolver.num_resolve());

  EXPECT_EQ("166.155.144.44:100", proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- nothing was logged.
  EXPECT_EQ(0u, log.GetSize());
  EXPECT_EQ(0u, request_log.GetSize());
}

// This test runs a weird PAC script that yields a never ending sequence
// of DNS resolves when restarting. Running it will hit the maximum
// DNS resolves per request limit (20) after which every DNS resolve will
// fail.
TEST_F(ProxyResolverV8TracingWrapperTest, InfiniteDNSSequence) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  for (int i = 0; i < 21; ++i) {
    host_resolver.SetResult("host" + std::to_string(i),
                            ProxyResolveDnsOperation::DNS_RESOLVE,
                            {IPAddress(166, 155, 144, 11)});
  }

  std::unique_ptr<ProxyResolver> resolver =
      CreateResolver(&log, &host_resolver, base::WrapUnique(error_observer),
                     "global_sideffects3.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,

                               callback.callback(), &req, request_log.bound());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ(20u, host_resolver.num_resolve());

  EXPECT_EQ(
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "166.155.144.11-166.155.144.11-166.155.144.11-166.155.144.11-"
      "null:21",
      proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- 1 alert was logged.
  EXPECT_EQ(1u, log.GetSize());
  EXPECT_EQ(1u, request_log.GetSize());
}

// This test runs a weird PAC script that yields a never ending sequence
// of DNS resolves when restarting. Running it will hit the maximum
// DNS resolves per request limit (20) after which every DNS resolve will
// fail.
TEST_F(ProxyResolverV8TracingWrapperTest, InfiniteDNSSequence2) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.SetResult(GetHostName(),
                          ProxyResolveDnsOperation::MY_IP_ADDRESS,
                          {IPAddress(122, 133, 144, 155)});
  for (int i = 0; i < 21; ++i) {
    host_resolver.SetResult("host" + std::to_string(i),
                            ProxyResolveDnsOperation::DNS_RESOLVE,
                            {IPAddress(166, 155, 144, 11)});
  }

  std::unique_ptr<ProxyResolver> resolver =
      CreateResolver(&log, &host_resolver, base::WrapUnique(error_observer),
                     "global_sideffects4.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ(20u, host_resolver.num_resolve());

  EXPECT_EQ("null21:34", proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  // Check the NetLogs -- 1 alert was logged.
  EXPECT_EQ(1u, log.GetSize());
  EXPECT_EQ(1u, request_log.GetSize());
}

void DnsDuringInitHelper(bool synchronous_host_resolver) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver(synchronous_host_resolver);
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(91, 13, 12, 1)});
  host_resolver.SetResult("host2", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(91, 13, 12, 2)});

  std::unique_ptr<ProxyResolver> resolver =
      CreateResolver(&log, &host_resolver, base::WrapUnique(error_observer),
                     "dns_during_init.js");

  // Initialization did 2 dnsResolves.
  EXPECT_EQ(2u, host_resolver.num_resolve());

  host_resolver.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(145, 88, 13, 3)});
  host_resolver.SetResult("host2", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(137, 89, 8, 45)});

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                               callback.callback(), &req, request_log.bound());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // Fetched host1 and host2 again, since the ones done during initialization
  // should not have been cached.
  EXPECT_EQ(4u, host_resolver.num_resolve());

  EXPECT_EQ("91.13.12.1-91.13.12.2-145.88.13.3-137.89.8.45:99",
            proxy_info.proxy_server().ToURI());

  // Check the NetLogs -- the script generated 2 alerts during initialization.
  EXPECT_EQ(0u, request_log.GetSize());
  TestNetLogEntry::List entries;
  log.GetEntries(&entries);

  ASSERT_EQ(2u, entries.size());
  EXPECT_TRUE(LogContainsEvent(entries, 0,
                               NetLogEventType::PAC_JAVASCRIPT_ALERT,
                               NetLogEventPhase::NONE));
  EXPECT_TRUE(LogContainsEvent(entries, 1,
                               NetLogEventType::PAC_JAVASCRIPT_ALERT,
                               NetLogEventPhase::NONE));

  EXPECT_EQ("{\"message\":\"Watsup\"}", entries[0].GetParamsJson());
  EXPECT_EQ("{\"message\":\"Watsup2\"}", entries[1].GetParamsJson());
}

// Tests a PAC script which does DNS resolves during initialization.
TEST_F(ProxyResolverV8TracingWrapperTest, DnsDuringInit) {
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
TEST_F(ProxyResolverV8TracingWrapperTest, CancelAll) {
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.FailAll();

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      nullptr, &host_resolver, base::WrapUnique(error_observer), "dns.js");

  const size_t kNumRequests = 5;
  ProxyInfo proxy_info[kNumRequests];
  std::unique_ptr<ProxyResolver::Request> request[kNumRequests];

  for (size_t i = 0; i < kNumRequests; ++i) {
    int rv = resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info[i],
                                      base::Bind(&CrashCallback), &request[i],
                                      NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  }

  for (size_t i = 0; i < kNumRequests; ++i) {
    request[i].reset();
  }
}

// Note the execution order for this test can vary. Since multiple
// threads are involved, the cancellation may be received a different
// times.
TEST_F(ProxyResolverV8TracingWrapperTest, CancelSome) {
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.FailAll();

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      nullptr, &host_resolver, base::WrapUnique(error_observer), "dns.js");

  ProxyInfo proxy_info1;
  ProxyInfo proxy_info2;
  std::unique_ptr<ProxyResolver::Request> request1;
  std::unique_ptr<ProxyResolver::Request> request2;
  TestCompletionCallback callback;

  int rv = resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info1,
                                    base::Bind(&CrashCallback), &request1,
                                    NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  rv = resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info2,
                                callback.callback(), &request2,
                                NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  request1.reset();

  EXPECT_THAT(callback.WaitForResult(), IsOk());
}

// Cancel a request after it has finished running on the worker thread, and has
// posted a task the completion task back to origin thread.
TEST_F(ProxyResolverV8TracingWrapperTest, CancelWhilePendingCompletionTask) {
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.FailAll();

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      nullptr, &host_resolver, base::WrapUnique(error_observer), "error.js");

  ProxyInfo proxy_info1;
  ProxyInfo proxy_info2;
  std::unique_ptr<ProxyResolver::Request> request1;
  std::unique_ptr<ProxyResolver::Request> request2;
  TestCompletionCallback callback;

  int rv = resolver->GetProxyForURL(GURL("http://throw-an-error/"),
                                    &proxy_info1, base::Bind(&CrashCallback),
                                    &request1, NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Wait until the first request has finished running on the worker thread.
  // Cancel the first request, while it has a pending completion task on
  // the origin thread. Reset deletes Request object which cancels the request.
  error_observer->RunOnError(
      base::Bind(&std::unique_ptr<ProxyResolver::Request>::reset,
                 base::Unretained(&request1), nullptr));

  // Start another request, to make sure it is able to complete.
  rv = resolver->GetProxyForURL(GURL("http://i-have-no-idea-what-im-doing/"),
                                &proxy_info2, callback.callback(), &request2,
                                NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  EXPECT_THAT(callback.WaitForResult(), IsOk());

  EXPECT_EQ("i-approve-this-message:42", proxy_info2.proxy_server().ToURI());
}

// This cancellation test exercises a more predictable cancellation codepath --
// when the request has an outstanding DNS request in flight.
TEST_F(ProxyResolverV8TracingWrapperTest,
       CancelWhileOutstandingNonBlockingDns) {
  base::RunLoop run_loop1;
  HangingProxyHostResolver host_resolver(run_loop1.QuitClosure());
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      nullptr, &host_resolver, base::WrapUnique(error_observer), "dns.js");

  ProxyInfo proxy_info1;
  ProxyInfo proxy_info2;
  std::unique_ptr<ProxyResolver::Request> request1;
  std::unique_ptr<ProxyResolver::Request> request2;

  int rv = resolver->GetProxyForURL(GURL("http://foo/req1"), &proxy_info1,
                                    base::Bind(&CrashCallback), &request1,
                                    NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  run_loop1.Run();

  base::RunLoop run_loop2;
  host_resolver.set_hang_callback(run_loop2.QuitClosure());
  rv = resolver->GetProxyForURL(GURL("http://foo/req2"), &proxy_info2,
                                base::Bind(&CrashCallback), &request2,
                                NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  run_loop2.Run();

  request1.reset();
  request2.reset();

  EXPECT_EQ(2, host_resolver.num_cancelled_requests());

  // After leaving this scope, the ProxyResolver is destroyed.
  // This should not cause any problems, as the outstanding work
  // should have been cancelled.
}

void CancelRequestAndPause(std::unique_ptr<ProxyResolver::Request>* request,
                           base::RunLoop* run_loop) {
  request->reset();

  // Sleep for a little bit. This makes it more likely for the worker
  // thread to have returned from its call, and serves as a regression
  // test for http://crbug.com/173373.
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(30));

  run_loop->Quit();
}

// In non-blocking mode, the worker thread actually does block for
// a short time to see if the result is in the DNS cache. Test
// cancellation while the worker thread is waiting on this event.
TEST_F(ProxyResolverV8TracingWrapperTest, CancelWhileBlockedInNonBlockingDns) {
  HangingProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      nullptr, &host_resolver, base::WrapUnique(error_observer), "dns.js");

  ProxyInfo proxy_info;
  std::unique_ptr<ProxyResolver::Request> request;

  base::RunLoop run_loop;
  host_resolver.set_hang_callback(
      base::BindRepeating(&CancelRequestAndPause, &request, &run_loop));

  int rv = resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                                    base::Bind(&CrashCallback), &request,
                                    NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  run_loop.Run();
}

// Cancel the request while there is a pending DNS request, however before
// the request is sent to the host resolver.
TEST_F(ProxyResolverV8TracingWrapperTest, CancelWhileBlockedInNonBlockingDns2) {
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      nullptr, &host_resolver, base::WrapUnique(error_observer), "dns.js");

  ProxyInfo proxy_info;
  std::unique_ptr<ProxyResolver::Request> request;

  int rv = resolver->GetProxyForURL(GURL("http://foo/"), &proxy_info,
                                    base::Bind(&CrashCallback), &request,
                                    NetLogWithSource());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  // Wait a bit, so the DNS task has hopefully been posted. The test will
  // work whatever the delay is here, but it is most useful if the delay
  // is large enough to allow a task to be posted back.
  base::PlatformThread::Sleep(base::TimeDelta::FromMilliseconds(10));
  request.reset();

  EXPECT_EQ(0u, host_resolver.num_resolve());
}

TEST_F(ProxyResolverV8TracingWrapperTest,
       CancelCreateResolverWhileOutstandingBlockingDns) {
  base::RunLoop run_loop;
  HangingProxyHostResolver host_resolver(run_loop.QuitClosure());
  MockErrorObserver* error_observer = new MockErrorObserver;

  ProxyResolverFactoryV8TracingWrapper factory(
      &host_resolver, nullptr,
      base::Bind(&ReturnErrorObserver,
                 base::Passed(base::WrapUnique(error_observer))));

  std::unique_ptr<ProxyResolver> resolver;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  int rv = factory.CreateProxyResolver(LoadScriptData("dns_during_init.js"),
                                       &resolver, base::Bind(&CrashCallback),
                                       &request);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));

  run_loop.Run();

  request.reset();
  EXPECT_EQ(1, host_resolver.num_cancelled_requests());
}

TEST_F(ProxyResolverV8TracingWrapperTest,
       DeleteFactoryWhileOutstandingBlockingDns) {
  base::RunLoop run_loop;
  HangingProxyHostResolver host_resolver(run_loop.QuitClosure());
  MockErrorObserver* error_observer = new MockErrorObserver;

  std::unique_ptr<ProxyResolver> resolver;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  {
    ProxyResolverFactoryV8TracingWrapper factory(
        &host_resolver, nullptr,
        base::Bind(&ReturnErrorObserver,
                   base::Passed(base::WrapUnique(error_observer))));

    int rv = factory.CreateProxyResolver(LoadScriptData("dns_during_init.js"),
                                         &resolver, base::Bind(&CrashCallback),
                                         &request);
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
    run_loop.Run();
  }
  EXPECT_EQ(1, host_resolver.num_cancelled_requests());
}

TEST_F(ProxyResolverV8TracingWrapperTest, ErrorLoadingScript) {
  HangingProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  ProxyResolverFactoryV8TracingWrapper factory(
      &host_resolver, nullptr,
      base::Bind(&ReturnErrorObserver,
                 base::Passed(base::WrapUnique(error_observer))));

  std::unique_ptr<ProxyResolver> resolver;
  std::unique_ptr<ProxyResolverFactory::Request> request;
  TestCompletionCallback callback;
  int rv =
      factory.CreateProxyResolver(LoadScriptData("error_on_load.js"), &resolver,
                                  callback.callback(), &request);
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsError(ERR_PAC_SCRIPT_FAILED));
  EXPECT_FALSE(resolver);
}

// This tests that the execution of a PAC script is terminated when the DNS
// dependencies are missing. If the test fails, then it will hang.
TEST_F(ProxyResolverV8TracingWrapperTest, Terminate) {
  TestNetLog log;
  BoundTestNetLog request_log;
  MockProxyHostResolver host_resolver;
  MockErrorObserver* error_observer = new MockErrorObserver;

  host_resolver.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE,
                          {IPAddress(182, 111, 0, 222)});
  host_resolver.SetResult("host2", ProxyResolveDnsOperation::DNS_RESOLVE_EX,
                          {IPAddress(111, 33, 44, 55)});

  std::unique_ptr<ProxyResolver> resolver = CreateResolver(
      &log, &host_resolver, base::WrapUnique(error_observer), "terminate.js");

  TestCompletionCallback callback;
  ProxyInfo proxy_info;

  std::unique_ptr<ProxyResolver::Request> req;
  int rv =
      resolver->GetProxyForURL(GURL("http://foopy/req1"), &proxy_info,
                               callback.callback(), &req, request_log.bound());

  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_THAT(callback.WaitForResult(), IsOk());

  // The test does 2 DNS resolutions.
  EXPECT_EQ(2u, host_resolver.num_resolve());

  EXPECT_EQ("foopy:3", proxy_info.proxy_server().ToURI());

  // No errors.
  EXPECT_EQ("", error_observer->GetOutput());

  EXPECT_EQ(0u, log.GetSize());
  EXPECT_EQ(0u, request_log.GetSize());
}

// Tests that multiple instances of ProxyResolverV8TracingWrapper can coexist
// and run correctly at the same time. This is relevant because at the moment
// (time this test was written) each ProxyResolverV8TracingWrapper creates its
// own thread to run V8 on, however each thread is operating on the same
// v8::Isolate.
TEST_F(ProxyResolverV8TracingWrapperTest, MultipleResolvers) {
  // ------------------------
  // Setup resolver0
  // ------------------------
  MockProxyHostResolver host_resolver0;
  host_resolver0.SetResult(GetHostName(),
                           ProxyResolveDnsOperation::MY_IP_ADDRESS,
                           {IPAddress(122, 133, 144, 155)});
  host_resolver0.SetResult(GetHostName(),
                           ProxyResolveDnsOperation::MY_IP_ADDRESS_EX,
                           {IPAddress(133, 122, 100, 200)});
  host_resolver0.SetError("", ProxyResolveDnsOperation::DNS_RESOLVE);
  host_resolver0.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE,
                           {IPAddress(166, 155, 144, 44)});
  IPAddress v6_local;
  ASSERT_TRUE(v6_local.AssignFromIPLiteral("::1"));
  host_resolver0.SetResult("host1", ProxyResolveDnsOperation::DNS_RESOLVE_EX,
                           {v6_local, IPAddress(192, 168, 1, 1)});
  host_resolver0.SetError("host2", ProxyResolveDnsOperation::DNS_RESOLVE);
  host_resolver0.SetResult("host3", ProxyResolveDnsOperation::DNS_RESOLVE,
                           {IPAddress(166, 155, 144, 33)});
  host_resolver0.SetError("host6", ProxyResolveDnsOperation::DNS_RESOLVE_EX);
  std::unique_ptr<ProxyResolver> resolver0 =
      CreateResolver(nullptr, &host_resolver0,
                     std::make_unique<MockErrorObserver>(), "dns.js");

  // ------------------------
  // Setup resolver1
  // ------------------------
  std::unique_ptr<ProxyResolver> resolver1 =
      CreateResolver(nullptr, &host_resolver0,
                     std::make_unique<MockErrorObserver>(), "dns.js");

  // ------------------------
  // Setup resolver2
  // ------------------------
  std::unique_ptr<ProxyResolver> resolver2 =
      CreateResolver(nullptr, &host_resolver0,
                     std::make_unique<MockErrorObserver>(), "simple.js");

  // ------------------------
  // Setup resolver3
  // ------------------------
  MockProxyHostResolver host_resolver3;
  host_resolver3.SetResult("foo", ProxyResolveDnsOperation::DNS_RESOLVE,
                           {IPAddress(166, 155, 144, 33)});
  std::unique_ptr<ProxyResolver> resolver3 =
      CreateResolver(nullptr, &host_resolver3,
                     std::make_unique<MockErrorObserver>(), "simple_dns.js");

  // ------------------------
  // Queue up work for each resolver (which will be running in parallel).
  // ------------------------

  ProxyResolver* resolver[] = {
      resolver0.get(), resolver1.get(), resolver2.get(), resolver3.get(),
  };

  const size_t kNumResolvers = base::size(resolver);
  const size_t kNumIterations = 20;
  const size_t kNumResults = kNumResolvers * kNumIterations;
  TestCompletionCallback callback[kNumResults];
  ProxyInfo proxy_info[kNumResults];
  std::unique_ptr<ProxyResolver::Request> request[kNumResults];

  for (size_t i = 0; i < kNumResults; ++i) {
    size_t resolver_i = i % kNumResolvers;
    int rv = resolver[resolver_i]->GetProxyForURL(
        GURL("http://foo/"), &proxy_info[i], callback[i].callback(),
        &request[i], NetLogWithSource());
    EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  }

  // ------------------------
  // Verify all of the results.
  // ------------------------

  const char* kExpectedForDnsJs =
      "122.133.144.155-"  // myIpAddress()
      "null-"             // dnsResolve('')
      "__1_192.168.1.1-"  // dnsResolveEx('host1')
      "null-"             // dnsResolve('host2')
      "166.155.144.33-"   // dnsResolve('host3')
      "122.133.144.155-"  // myIpAddress()
      "166.155.144.33-"   // dnsResolve('host3')
      "__1_192.168.1.1-"  // dnsResolveEx('host1')
      "122.133.144.155-"  // myIpAddress()
      "null-"             // dnsResolve('host2')
      "-"                 // dnsResolveEx('host6')
      "133.122.100.200-"  // myIpAddressEx()
      "166.155.144.44"    // dnsResolve('host1')
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
