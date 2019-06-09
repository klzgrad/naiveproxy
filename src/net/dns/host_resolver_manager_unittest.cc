// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_resolver_manager.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/condition_variable.h"
#include "base/synchronization/lock.h"
#include "base/test/bind_test_util.h"
#include "base/test/simple_test_clock.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/test/test_timeouts.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/timer/mock_timer.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/mock_network_change_notifier.h"
#include "net/base/net_errors.h"
#include "net/dns/dns_client.h"
#include "net/dns/dns_config.h"
#include "net/dns/dns_test_util.h"
#include "net/dns/dns_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/dns/mock_mdns_client.h"
#include "net/dns/mock_mdns_socket_factory.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/log/test_net_log.h"
#include "net/test/gtest_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(ENABLE_MDNS)
#include "net/dns/mdns_client_impl.h"
#endif  // BUILDFLAG(ENABLE_MDNS)

using net::test::IsError;
using net::test::IsOk;
using ::testing::_;
using ::testing::Between;
using ::testing::ByMove;
using ::testing::NotNull;
using ::testing::Return;

namespace net {

namespace {

const size_t kMaxJobs = 10u;
const size_t kMaxRetryAttempts = 4u;

ProcTaskParams DefaultParams(HostResolverProc* resolver_proc) {
  return ProcTaskParams(resolver_proc, kMaxRetryAttempts);
}

// A HostResolverProc that pushes each host mapped into a list and allows
// waiting for a specific number of requests. Unlike RuleBasedHostResolverProc
// it never calls SystemHostResolverCall. By default resolves all hostnames to
// "127.0.0.1". After AddRule(), it resolves only names explicitly specified.
class MockHostResolverProc : public HostResolverProc {
 public:
  struct ResolveKey {
    ResolveKey(const std::string& hostname,
               AddressFamily address_family,
               HostResolverFlags flags)
        : hostname(hostname), address_family(address_family), flags(flags) {}
    bool operator<(const ResolveKey& other) const {
      return std::tie(address_family, hostname, flags) <
             std::tie(other.address_family, other.hostname, other.flags);
    }
    std::string hostname;
    AddressFamily address_family;
    HostResolverFlags flags;
  };

  typedef std::vector<ResolveKey> CaptureList;

  MockHostResolverProc()
      : HostResolverProc(nullptr),
        num_requests_waiting_(0),
        num_slots_available_(0),
        requests_waiting_(&lock_),
        slots_available_(&lock_) {}

  // Waits until |count| calls to |Resolve| are blocked. Returns false when
  // timed out.
  bool WaitFor(unsigned count) {
    base::AutoLock lock(lock_);
    base::Time start_time = base::Time::Now();
    while (num_requests_waiting_ < count) {
      requests_waiting_.TimedWait(TestTimeouts::action_timeout());
      if (base::Time::Now() > start_time + TestTimeouts::action_timeout())
        return false;
    }
    return true;
  }

  // Signals |count| waiting calls to |Resolve|. First come first served.
  void SignalMultiple(unsigned count) {
    base::AutoLock lock(lock_);
    num_slots_available_ += count;
    slots_available_.Broadcast();
  }

  // Signals all waiting calls to |Resolve|. Beware of races.
  void SignalAll() {
    base::AutoLock lock(lock_);
    num_slots_available_ = num_requests_waiting_;
    slots_available_.Broadcast();
  }

  void AddRule(const std::string& hostname,
               AddressFamily family,
               const AddressList& result,
               HostResolverFlags flags = 0) {
    base::AutoLock lock(lock_);
    rules_[ResolveKey(hostname, family, flags)] = result;
  }

  void AddRule(const std::string& hostname,
               AddressFamily family,
               const std::string& ip_list,
               HostResolverFlags flags = 0,
               const std::string& canonical_name = "") {
    AddressList result;
    int rv = ParseAddressList(ip_list, canonical_name, &result);
    DCHECK_EQ(OK, rv);
    AddRule(hostname, family, result, flags);
  }

  void AddRuleForAllFamilies(const std::string& hostname,
                             const std::string& ip_list,
                             HostResolverFlags flags = 0,
                             const std::string& canonical_name = "") {
    AddressList result;
    int rv = ParseAddressList(ip_list, canonical_name, &result);
    DCHECK_EQ(OK, rv);
    AddRule(hostname, ADDRESS_FAMILY_UNSPECIFIED, result, flags);
    AddRule(hostname, ADDRESS_FAMILY_IPV4, result, flags);
    AddRule(hostname, ADDRESS_FAMILY_IPV6, result, flags);
  }

  int Resolve(const std::string& hostname,
              AddressFamily address_family,
              HostResolverFlags host_resolver_flags,
              AddressList* addrlist,
              int* os_error) override {
    base::AutoLock lock(lock_);
    capture_list_.push_back(
        ResolveKey(hostname, address_family, host_resolver_flags));
    ++num_requests_waiting_;
    requests_waiting_.Broadcast();
    {
      base::ScopedAllowBaseSyncPrimitivesForTesting
          scoped_allow_base_sync_primitives;
      while (!num_slots_available_)
        slots_available_.Wait();
    }
    DCHECK_GT(num_requests_waiting_, 0u);
    --num_slots_available_;
    --num_requests_waiting_;
    if (rules_.empty()) {
      int rv = ParseAddressList("127.0.0.1", std::string(), addrlist);
      DCHECK_EQ(OK, rv);
      return OK;
    }
    // Ignore HOST_RESOLVER_SYSTEM_ONLY, since it should have no impact on
    // whether a rule matches. It should only affect cache lookups.
    ResolveKey key(hostname, address_family,
                   host_resolver_flags & ~HOST_RESOLVER_SYSTEM_ONLY);
    if (rules_.count(key) == 0)
      return ERR_NAME_NOT_RESOLVED;
    *addrlist = rules_[key];
    return OK;
  }

  CaptureList GetCaptureList() const {
    CaptureList copy;
    {
      base::AutoLock lock(lock_);
      copy = capture_list_;
    }
    return copy;
  }

  bool HasBlockedRequests() const {
    base::AutoLock lock(lock_);
    return num_requests_waiting_ > num_slots_available_;
  }

 protected:
  ~MockHostResolverProc() override = default;

 private:
  mutable base::Lock lock_;
  std::map<ResolveKey, AddressList> rules_;
  CaptureList capture_list_;
  unsigned num_requests_waiting_;
  unsigned num_slots_available_;
  base::ConditionVariable requests_waiting_;
  base::ConditionVariable slots_available_;

  DISALLOW_COPY_AND_ASSIGN(MockHostResolverProc);
};

class ResolveHostResponseHelper {
 public:
  using Callback =
      base::OnceCallback<void(CompletionOnceCallback completion_callback,
                              int error)>;

  ResolveHostResponseHelper() {}
  explicit ResolveHostResponseHelper(
      std::unique_ptr<HostResolverManager::CancellableRequest> request)
      : request_(std::move(request)) {
    result_error_ = request_->Start(base::BindOnce(
        &ResolveHostResponseHelper::OnComplete, base::Unretained(this)));
  }
  ResolveHostResponseHelper(
      std::unique_ptr<HostResolverManager::CancellableRequest> request,
      Callback custom_callback)
      : request_(std::move(request)) {
    result_error_ = request_->Start(
        base::BindOnce(std::move(custom_callback),
                       base::BindOnce(&ResolveHostResponseHelper::OnComplete,
                                      base::Unretained(this))));
  }

  bool complete() const { return result_error_ != ERR_IO_PENDING; }
  int result_error() {
    WaitForCompletion();
    return result_error_;
  }

  HostResolverManager::CancellableRequest* request() { return request_.get(); }

  void CancelRequest() {
    DCHECK(request_);
    DCHECK(!complete());

    request_ = nullptr;
  }

  void OnComplete(int error) {
    DCHECK(!complete());
    result_error_ = error;

    run_loop_.Quit();
  }

 private:
  void WaitForCompletion() {
    DCHECK(request_);
    if (complete()) {
      return;
    }
    run_loop_.Run();
    DCHECK(complete());
  }

  std::unique_ptr<HostResolverManager::CancellableRequest> request_;
  int result_error_ = ERR_IO_PENDING;
  base::RunLoop run_loop_;

  DISALLOW_COPY_AND_ASSIGN(ResolveHostResponseHelper);
};

// Using LookupAttemptHostResolverProc simulate very long lookups, and control
// which attempt resolves the host.
class LookupAttemptHostResolverProc : public HostResolverProc {
 public:
  LookupAttemptHostResolverProc(HostResolverProc* previous,
                                int attempt_number_to_resolve,
                                int total_attempts)
      : HostResolverProc(previous),
        attempt_number_to_resolve_(attempt_number_to_resolve),
        current_attempt_number_(0),
        total_attempts_(total_attempts),
        total_attempts_resolved_(0),
        resolved_attempt_number_(0),
        num_attempts_waiting_(0),
        all_done_(&lock_),
        blocked_attempt_signal_(&lock_) {}

  // Test harness will wait for all attempts to finish before checking the
  // results.
  void WaitForAllAttemptsToFinish() {
    base::AutoLock auto_lock(lock_);
    while (total_attempts_resolved_ != total_attempts_) {
      all_done_.Wait();
    }
  }

  void WaitForNAttemptsToBeBlocked(int n) {
    base::AutoLock auto_lock(lock_);
    while (num_attempts_waiting_ < n) {
      blocked_attempt_signal_.Wait();
    }
  }

  // All attempts will wait for an attempt to resolve the host.
  void WaitForAnAttemptToComplete() {
    {
      base::AutoLock auto_lock(lock_);
      base::ScopedAllowBaseSyncPrimitivesForTesting
          scoped_allow_base_sync_primitives;
      while (resolved_attempt_number_ == 0)
        all_done_.Wait();
    }
    all_done_.Broadcast();  // Tell all waiting attempts to proceed.
  }

  // Returns the number of attempts that have finished the Resolve() method.
  int total_attempts_resolved() { return total_attempts_resolved_; }

  // Returns the first attempt that that has resolved the host.
  int resolved_attempt_number() { return resolved_attempt_number_; }

  // Returns the current number of blocked attempts.
  int num_attempts_waiting() { return num_attempts_waiting_; }

  // HostResolverProc methods.
  int Resolve(const std::string& host,
              AddressFamily address_family,
              HostResolverFlags host_resolver_flags,
              AddressList* addrlist,
              int* os_error) override {
    bool wait_for_right_attempt_to_complete = true;
    {
      base::AutoLock auto_lock(lock_);
      ++current_attempt_number_;
      ++num_attempts_waiting_;
      if (current_attempt_number_ == attempt_number_to_resolve_) {
        resolved_attempt_number_ = current_attempt_number_;
        wait_for_right_attempt_to_complete = false;
      }
    }

    blocked_attempt_signal_.Broadcast();

    if (wait_for_right_attempt_to_complete)
      // Wait for the attempt_number_to_resolve_ attempt to resolve.
      WaitForAnAttemptToComplete();

    int result = ResolveUsingPrevious(host, address_family, host_resolver_flags,
                                      addrlist, os_error);

    {
      base::AutoLock auto_lock(lock_);
      ++total_attempts_resolved_;
      --num_attempts_waiting_;
    }

    all_done_.Broadcast();  // Tell all attempts to proceed.

    // Since any negative number is considered a network error, with -1 having
    // special meaning (ERR_IO_PENDING). We could return the attempt that has
    // resolved the host as a negative number. For example, if attempt number 3
    // resolves the host, then this method returns -4.
    if (result == OK)
      return -1 - resolved_attempt_number_;
    else
      return result;
  }

 protected:
  ~LookupAttemptHostResolverProc() override = default;

 private:
  int attempt_number_to_resolve_;
  int current_attempt_number_;  // Incremented whenever Resolve is called.
  int total_attempts_;
  int total_attempts_resolved_;
  int resolved_attempt_number_;
  int num_attempts_waiting_;

  // All attempts wait for right attempt to be resolve.
  base::Lock lock_;
  base::ConditionVariable all_done_;
  base::ConditionVariable blocked_attempt_signal_;
};

// TestHostResolverManager's sole purpose is to mock the IPv6 reachability test.
// By default, this pretends that IPv6 is globally reachable.
// This class is necessary so unit tests run the same on dual-stack machines as
// well as IPv4 only machines.
class TestHostResolverManager : public HostResolverManager {
 public:
  TestHostResolverManager(const HostResolver::ManagerOptions& options,
                          NetLog* net_log)
      : TestHostResolverManager(options, net_log, true) {}

  TestHostResolverManager(
      const HostResolver::ManagerOptions& options,
      NetLog* net_log,
      bool ipv6_reachable,
      DnsClientFactory dns_client_factory_for_testing = base::NullCallback())
      : HostResolverManager(options, net_log, dns_client_factory_for_testing),
        ipv6_reachable_(ipv6_reachable) {}

  ~TestHostResolverManager() override = default;

 private:
  const bool ipv6_reachable_;

  bool IsGloballyReachable(const IPAddress& dest,
                           const NetLogWithSource& net_log) override {
    return ipv6_reachable_;
  }
};

bool HasAddress(const IPAddress& search_address, const AddressList& addresses) {
  for (const auto& address : addresses) {
    if (search_address == address.address())
      return true;
  }
  return false;
}

void TestBothLoopbackIPs(const std::string& host) {
  AddressList addresses;
  EXPECT_TRUE(ResolveLocalHostname(host, &addresses));
  EXPECT_EQ(2u, addresses.size());
  EXPECT_TRUE(HasAddress(IPAddress::IPv4Localhost(), addresses));
  EXPECT_TRUE(HasAddress(IPAddress::IPv6Localhost(), addresses));
}

void TestIPv6LoopbackOnly(const std::string& host) {
  AddressList addresses;
  EXPECT_TRUE(ResolveLocalHostname(host, &addresses));
  EXPECT_EQ(1u, addresses.size());
  EXPECT_TRUE(HasAddress(IPAddress::IPv6Localhost(), addresses));
}

}  // namespace

class HostResolverManagerTest : public TestWithScopedTaskEnvironment {
 public:
  static const int kDefaultPort = 80;

  HostResolverManagerTest() : proc_(new MockHostResolverProc()) {}

  void CreateResolver(bool check_ipv6_on_wifi = true) {
    CreateResolverWithLimitsAndParams(kMaxJobs, DefaultParams(proc_.get()),
                                      true /* ipv6_reachable */,
                                      check_ipv6_on_wifi);
  }

  void DestroyResolver() {
    if (!resolver_)
      return;

    if (host_cache_)
      resolver_->RemoveHostCacheInvalidator(host_cache_->invalidator());
    resolver_ = nullptr;
  }

  // This HostResolverManager will only allow 1 outstanding resolve at a time
  // and perform no retries.
  void CreateSerialResolver(bool check_ipv6_on_wifi = true) {
    ProcTaskParams params = DefaultParams(proc_.get());
    params.max_retry_attempts = 0u;
    CreateResolverWithLimitsAndParams(1u, params, true /* ipv6_reachable */,
                                      check_ipv6_on_wifi);
  }

 protected:
  // testing::Test implementation:
  void SetUp() override {
    host_cache_ = HostCache::CreateDefaultCache();
    CreateResolver();
    request_context_ = std::make_unique<TestURLRequestContext>();
  }

  void TearDown() override {
    if (resolver_) {
      EXPECT_EQ(0u, resolver_->num_running_dispatcher_jobs_for_tests());
      if (host_cache_)
        resolver_->RemoveHostCacheInvalidator(host_cache_->invalidator());
    }
    EXPECT_FALSE(proc_->HasBlockedRequests());
  }

  void CreateResolverWithLimitsAndParams(size_t max_concurrent_resolves,
                                         const ProcTaskParams& params,
                                         bool ipv6_reachable,
                                         bool check_ipv6_on_wifi) {
    HostResolver::ManagerOptions options = DefaultOptions();
    options.max_concurrent_resolves = max_concurrent_resolves;
    options.check_ipv6_on_wifi = check_ipv6_on_wifi;

    CreateResolverWithOptionsAndParams(std::move(options), params,
                                       ipv6_reachable);
  }

  virtual HostResolver::ManagerOptions DefaultOptions() {
    HostResolver::ManagerOptions options;
    options.max_concurrent_resolves = kMaxJobs;
    options.max_system_retry_attempts = kMaxRetryAttempts;
    return options;
  }

  virtual void CreateResolverWithOptionsAndParams(
      HostResolver::ManagerOptions options,
      const ProcTaskParams& params,
      bool ipv6_reachable) {
    // Use HostResolverManagerDnsTest if enabling DNS client.
    DCHECK(!options.dns_client_enabled);

    DestroyResolver();

    resolver_ = std::make_unique<TestHostResolverManager>(
        options, nullptr /* net_log */, ipv6_reachable);
    resolver_->set_proc_params_for_test(params);

    if (host_cache_)
      resolver_->AddHostCacheInvalidator(host_cache_->invalidator());
  }

  // Friendship is not inherited, so use proxies to access those.
  size_t num_running_dispatcher_jobs() const {
    DCHECK(resolver_.get());
    return resolver_->num_running_dispatcher_jobs_for_tests();
  }

  void set_allow_fallback_to_proctask(bool allow_fallback_to_proctask) {
    DCHECK(resolver_.get());
    resolver_->allow_fallback_to_proctask_ = allow_fallback_to_proctask;
  }

  static unsigned maximum_dns_failures() {
    return HostResolverManager::kMaximumDnsFailures;
  }

  bool IsIPv6Reachable(const NetLogWithSource& net_log) {
    return resolver_->IsIPv6Reachable(net_log);
  }

  const std::pair<const HostCache::Key, HostCache::Entry>* GetCacheHit(
      const HostCache::Key& key) {
    DCHECK(host_cache_);
    return host_cache_->LookupStale(key, base::TimeTicks(), nullptr,
                                    false /* ignore_secure */);
  }

  void MakeCacheStale() {
    DCHECK(host_cache_);
    host_cache_->Invalidate();
  }

  IPEndPoint CreateExpected(const std::string& ip_literal, uint16_t port) {
    IPAddress ip;
    bool result = ip.AssignFromIPLiteral(ip_literal);
    DCHECK(result);
    return IPEndPoint(ip, port);
  }

  scoped_refptr<MockHostResolverProc> proc_;
  std::unique_ptr<HostResolverManager> resolver_;
  std::unique_ptr<URLRequestContext> request_context_;
  std::unique_ptr<HostCache> host_cache_;
};

TEST_F(HostResolverManagerTest, AsynchronousLookup) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.42", 80)));
  EXPECT_FALSE(response.request()->GetStaleInfo());

  EXPECT_EQ("just.testing", proc_->GetCaptureList()[0].hostname);

  const std::pair<const HostCache::Key, HostCache::Entry>* cache_result =
      GetCacheHit(HostCache::Key("just.testing", DnsQueryType::UNSPECIFIED,
                                 0 /* host_resolver_flags */,
                                 HostResolverSource::ANY));
  EXPECT_TRUE(cache_result);
}

TEST_F(HostResolverManagerTest, JobsClearedOnCompletion) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(1u, resolver_->num_jobs_for_testing());

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_EQ(0u, resolver_->num_jobs_for_testing());
}

TEST_F(HostResolverManagerTest, JobsClearedOnCompletion_MultipleRequests) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response2(resolver_->CreateRequest(
      HostPortPair("just.testing", 85), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(1u, resolver_->num_jobs_for_testing());

  EXPECT_THAT(response1.result_error(), IsOk());
  EXPECT_THAT(response2.result_error(), IsOk());
  EXPECT_EQ(0u, resolver_->num_jobs_for_testing());
}

TEST_F(HostResolverManagerTest, JobsClearedOnCompletion_Failure) {
  proc_->AddRuleForAllFamilies(std::string(),
                               "0.0.0.0");  // Default to failures.
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(1u, resolver_->num_jobs_for_testing());

  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_EQ(0u, resolver_->num_jobs_for_testing());
}

TEST_F(HostResolverManagerTest, JobsClearedOnCompletion_Abort) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(1u, resolver_->num_jobs_for_testing());

  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  proc_->SignalMultiple(1u);

  EXPECT_THAT(response.result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_EQ(0u, resolver_->num_jobs_for_testing());
}

TEST_F(HostResolverManagerTest, DnsQueryType) {
  proc_->AddRule("host", ADDRESS_FAMILY_IPV4, "192.168.1.20");
  proc_->AddRule("host", ADDRESS_FAMILY_IPV6, "::5");

  HostResolver::ResolveHostParameters parameters;

  parameters.dns_query_type = DnsQueryType::A;
  ResolveHostResponseHelper v4_response(resolver_->CreateRequest(
      HostPortPair("host", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  parameters.dns_query_type = DnsQueryType::AAAA;
  ResolveHostResponseHelper v6_response(resolver_->CreateRequest(
      HostPortPair("host", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  proc_->SignalMultiple(2u);

  EXPECT_THAT(v4_response.result_error(), IsOk());
  EXPECT_THAT(v4_response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.20", 80)));

  EXPECT_THAT(v6_response.result_error(), IsOk());
  EXPECT_THAT(v6_response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::5", 80)));
}

TEST_F(HostResolverManagerTest, LocalhostIPV4IPV6Lookup) {
  HostResolver::ResolveHostParameters parameters;

  parameters.dns_query_type = DnsQueryType::A;
  ResolveHostResponseHelper v6_v4_response(resolver_->CreateRequest(
      HostPortPair("localhost6", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v6_v4_response.result_error(), IsOk());
  EXPECT_THAT(v6_v4_response.request()->GetAddressResults().value().endpoints(),
              testing::IsEmpty());

  parameters.dns_query_type = DnsQueryType::AAAA;
  ResolveHostResponseHelper v6_v6_response(resolver_->CreateRequest(
      HostPortPair("localhost6", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v6_v6_response.result_error(), IsOk());
  EXPECT_THAT(v6_v6_response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::1", 80)));

  ResolveHostResponseHelper v6_unsp_response(resolver_->CreateRequest(
      HostPortPair("localhost6", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v6_unsp_response.result_error(), IsOk());
  EXPECT_THAT(
      v6_unsp_response.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("::1", 80)));

  parameters.dns_query_type = DnsQueryType::A;
  ResolveHostResponseHelper v4_v4_response(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v4_v4_response.result_error(), IsOk());
  EXPECT_THAT(v4_v4_response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));

  parameters.dns_query_type = DnsQueryType::AAAA;
  ResolveHostResponseHelper v4_v6_response(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v4_v6_response.result_error(), IsOk());
  EXPECT_THAT(v4_v6_response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::1", 80)));

  ResolveHostResponseHelper v4_unsp_response(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v4_unsp_response.result_error(), IsOk());
  EXPECT_THAT(
      v4_unsp_response.request()->GetAddressResults().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                    CreateExpected("::1", 80)));
}

TEST_F(HostResolverManagerTest, ResolveIPLiteralWithHostResolverSystemOnly) {
  const char kIpLiteral[] = "178.78.32.1";
  // Add a mapping to tell if the resolver proc was called (if it was called,
  // then the result will be the remapped value. Otherwise it will be the IP
  // literal).
  proc_->AddRuleForAllFamilies(kIpLiteral, "183.45.32.1");

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::SYSTEM;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair(kIpLiteral, 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  // IP literal resolution is expected to take precedence over source, so the
  // result is expected to be the input IP, not the result IP from the proc rule
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected(kIpLiteral, 80)));
  EXPECT_FALSE(response.request()->GetStaleInfo());
}

TEST_F(HostResolverManagerTest, EmptyListMeansNameNotResolved) {
  proc_->AddRuleForAllFamilies("just.testing", "");
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetStaleInfo());

  EXPECT_EQ("just.testing", proc_->GetCaptureList()[0].hostname);
}

TEST_F(HostResolverManagerTest, FailedAsynchronousLookup) {
  proc_->AddRuleForAllFamilies(std::string(),
                               "0.0.0.0");  // Default to failures.
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetStaleInfo());

  EXPECT_EQ("just.testing", proc_->GetCaptureList()[0].hostname);

  // Also test that the error is not cached.
  const std::pair<const HostCache::Key, HostCache::Entry>* cache_result =
      GetCacheHit(HostCache::Key("just.testing", DnsQueryType::UNSPECIFIED,
                                 0 /* host_resolver_flags */,
                                 HostResolverSource::ANY));
  EXPECT_FALSE(cache_result);
}

TEST_F(HostResolverManagerTest, AbortedAsynchronousLookup) {
  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response0.complete());
  ASSERT_TRUE(proc_->WaitFor(1u));

  // Resolver is destroyed while job is running on WorkerPool.
  DestroyResolver();

  proc_->SignalAll();

  // To ensure there was no spurious callback, complete with a new resolver.
  CreateResolver();
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  proc_->SignalMultiple(2u);

  EXPECT_THAT(response1.result_error(), IsOk());

  // This request was canceled.
  EXPECT_FALSE(response0.complete());
}

TEST_F(HostResolverManagerTest, NumericIPv4Address) {
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("127.1.2.3", 5555), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.1.2.3", 5555)));
}

TEST_F(HostResolverManagerTest, NumericIPv6Address) {
  // Resolve a plain IPv6 address.  Don't worry about [brackets], because
  // the caller should have removed them.
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("2001:db8::1", 5555), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("2001:db8::1", 5555)));
}

TEST_F(HostResolverManagerTest, EmptyHost) {
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair(std::string(), 5555), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
}

TEST_F(HostResolverManagerTest, EmptyDotsHost) {
  for (int i = 0; i < 16; ++i) {
    ResolveHostResponseHelper response(resolver_->CreateRequest(
        HostPortPair(std::string(i, '.'), 5555), NetLogWithSource(),
        base::nullopt, request_context_.get(), host_cache_.get()));

    EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
    EXPECT_FALSE(response.request()->GetAddressResults());
  }
}

TEST_F(HostResolverManagerTest, LongHost) {
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair(std::string(4097, 'a'), 5555), NetLogWithSource(),
      base::nullopt, request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
}

TEST_F(HostResolverManagerTest, DeDupeRequests) {
  // Start 5 requests, duplicating hosts "a" and "b". Since the resolver_proc is
  // blocked, these should all pile up until we signal it.
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 81), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 82), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 83), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  for (auto& response : responses) {
    ASSERT_FALSE(response->complete());
  }

  proc_->SignalMultiple(2u);  // One for "a", one for "b".

  for (auto& response : responses) {
    EXPECT_THAT(response->result_error(), IsOk());
  }
}

TEST_F(HostResolverManagerTest, CancelMultipleRequests) {
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 81), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 82), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 83), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  for (auto& response : responses) {
    ASSERT_FALSE(response->complete());
  }

  // Cancel everything except request for requests[3] ("a", 82).
  responses[0]->CancelRequest();
  responses[1]->CancelRequest();
  responses[2]->CancelRequest();
  responses[4]->CancelRequest();

  proc_->SignalMultiple(2u);  // One for "a", one for "b".

  EXPECT_THAT(responses[3]->result_error(), IsOk());

  EXPECT_FALSE(responses[0]->complete());
  EXPECT_FALSE(responses[1]->complete());
  EXPECT_FALSE(responses[2]->complete());
  EXPECT_FALSE(responses[4]->complete());
}

TEST_F(HostResolverManagerTest, CanceledRequestsReleaseJobSlots) {
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;

  // Fill up the dispatcher and queue.
  for (unsigned i = 0; i < kMaxJobs + 1; ++i) {
    std::string hostname = "a_";
    hostname[1] = 'a' + i;

    responses.emplace_back(
        std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
            HostPortPair(hostname, 80), NetLogWithSource(), base::nullopt,
            request_context_.get(), host_cache_.get())));
    ASSERT_FALSE(responses.back()->complete());

    responses.emplace_back(
        std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
            HostPortPair(hostname, 81), NetLogWithSource(), base::nullopt,
            request_context_.get(), host_cache_.get())));
    ASSERT_FALSE(responses.back()->complete());
  }

  ASSERT_TRUE(proc_->WaitFor(kMaxJobs));

  // Cancel all but last two.
  for (unsigned i = 0; i < responses.size() - 2; ++i) {
    responses[i]->CancelRequest();
  }

  ASSERT_TRUE(proc_->WaitFor(kMaxJobs + 1));

  proc_->SignalAll();

  size_t num_requests = responses.size();
  EXPECT_THAT(responses[num_requests - 1]->result_error(), IsOk());
  EXPECT_THAT(responses[num_requests - 2]->result_error(), IsOk());
  for (unsigned i = 0; i < num_requests - 2; ++i) {
    EXPECT_FALSE(responses[i]->complete());
  }
}

TEST_F(HostResolverManagerTest, CancelWithinCallback) {
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  auto custom_callback = base::BindLambdaForTesting(
      [&](CompletionOnceCallback completion_callback, int error) {
        for (auto& response : responses) {
          // Cancelling request is required to complete first, so that it can
          // attempt to cancel the others.  This test assumes all jobs are
          // completed in order.
          DCHECK(!response->complete());

          response->CancelRequest();
        }
        std::move(completion_callback).Run(error);
      });

  ResolveHostResponseHelper cancelling_response(
      resolver_->CreateRequest(HostPortPair("a", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      std::move(custom_callback));

  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 81), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 82), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  proc_->SignalMultiple(2u);  // One for "a". One for "finalrequest".

  EXPECT_THAT(cancelling_response.result_error(), IsOk());

  ResolveHostResponseHelper final_response(resolver_->CreateRequest(
      HostPortPair("finalrequest", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(final_response.result_error(), IsOk());

  for (auto& response : responses) {
    EXPECT_FALSE(response->complete());
  }
}

TEST_F(HostResolverManagerTest, DeleteWithinCallback) {
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  auto custom_callback = base::BindLambdaForTesting(
      [&](CompletionOnceCallback completion_callback, int error) {
        for (auto& response : responses) {
          // Deleting request is required to be first, so the other requests
          // will still be running to be deleted. This test assumes that the
          // Jobs will be Aborted in order and the requests in order within the
          // jobs.
          DCHECK(!response->complete());
        }

        DestroyResolver();
        std::move(completion_callback).Run(error);
      });

  ResolveHostResponseHelper deleting_response(
      resolver_->CreateRequest(HostPortPair("a", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      std::move(custom_callback));

  // Start additional requests to be cancelled as part of the first's deletion.
  // Assumes all requests for a job are handled in order so that the deleting
  // request will run first and cancel the rest.
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 81), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 82), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  proc_->SignalMultiple(3u);

  EXPECT_THAT(deleting_response.result_error(), IsOk());

  base::RunLoop().RunUntilIdle();
  for (auto& response : responses) {
    EXPECT_FALSE(response->complete());
  }
}

// Flaky on Fuchsia and Linux ASAN. crbug.com/930483
#if defined(OS_FUCHSIA) || defined(OS_LINUX)
#define MAYBE_DeleteWithinAbortedCallback DISABLED_DeleteWithinAbortedCallback
#else
#define MAYBE_DeleteWithinAbortedCallback DeleteWithinAbortedCallback
#endif
TEST_F(HostResolverManagerTest, MAYBE_DeleteWithinAbortedCallback) {
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  ResolveHostResponseHelper::Callback custom_callback =
      base::BindLambdaForTesting(
          [&](CompletionOnceCallback completion_callback, int error) {
            for (auto& response : responses) {
              // Deleting request is required to be first, so the other requests
              // will still be running to be deleted. This test assumes that the
              // Jobs will be Aborted in order and the requests in order within
              // the jobs.
              DCHECK(!response->complete());
            }
            DestroyResolver();
            std::move(completion_callback).Run(error);
          });

  ResolveHostResponseHelper deleting_response(
      resolver_->CreateRequest(HostPortPair("a", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      std::move(custom_callback));

  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 81), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 82), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 83), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  // Wait for all calls to queue up, trigger abort via IP address change, then
  // signal all the queued requests to let them all try to finish.
  EXPECT_TRUE(proc_->WaitFor(2u));
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  proc_->SignalAll();

  EXPECT_THAT(deleting_response.result_error(), IsError(ERR_NETWORK_CHANGED));
  base::RunLoop().RunUntilIdle();
  for (auto& response : responses) {
    EXPECT_FALSE(response->complete());
  }
}

TEST_F(HostResolverManagerTest, StartWithinCallback) {
  std::unique_ptr<ResolveHostResponseHelper> new_response;
  auto custom_callback = base::BindLambdaForTesting(
      [&](CompletionOnceCallback completion_callback, int error) {
        new_response = std::make_unique<ResolveHostResponseHelper>(
            resolver_->CreateRequest(
                HostPortPair("new", 70), NetLogWithSource(), base::nullopt,
                request_context_.get(), host_cache_.get()));
        std::move(completion_callback).Run(error);
      });

  ResolveHostResponseHelper starting_response(
      resolver_->CreateRequest(HostPortPair("a", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      std::move(custom_callback));

  proc_->SignalMultiple(2u);  // One for "a". One for "new".

  EXPECT_THAT(starting_response.result_error(), IsOk());
  EXPECT_THAT(new_response->result_error(), IsOk());
}

TEST_F(HostResolverManagerTest, StartWithinEvictionCallback) {
  CreateSerialResolver();
  resolver_->SetMaxQueuedJobsForTesting(2);

  std::unique_ptr<ResolveHostResponseHelper> new_response;
  auto custom_callback = base::BindLambdaForTesting(
      [&](CompletionOnceCallback completion_callback, int error) {
        new_response = std::make_unique<ResolveHostResponseHelper>(
            resolver_->CreateRequest(
                HostPortPair("new", 70), NetLogWithSource(), base::nullopt,
                request_context_.get(), host_cache_.get()));
        std::move(completion_callback).Run(error);
      });

  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("initial", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper evictee1_response(
      resolver_->CreateRequest(HostPortPair("evictee1", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      std::move(custom_callback));
  ResolveHostResponseHelper evictee2_response(resolver_->CreateRequest(
      HostPortPair("evictee2", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  // Now one running request ("initial") and two queued requests ("evictee1" and
  // "evictee2"). Any further requests will cause evictions.
  ResolveHostResponseHelper evictor_response(resolver_->CreateRequest(
      HostPortPair("evictor", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(evictee1_response.result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));

  // "new" should evict "evictee2"
  EXPECT_THAT(evictee2_response.result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));

  proc_->SignalMultiple(3u);

  EXPECT_THAT(initial_response.result_error(), IsOk());
  EXPECT_THAT(evictor_response.result_error(), IsOk());
  EXPECT_THAT(new_response->result_error(), IsOk());
}

// Test where we start a new request within an eviction callback that itself
// evicts the first evictor.
TEST_F(HostResolverManagerTest, StartWithinEvictionCallback_DoubleEviction) {
  CreateSerialResolver();
  resolver_->SetMaxQueuedJobsForTesting(1);

  std::unique_ptr<ResolveHostResponseHelper> new_response;
  auto custom_callback = base::BindLambdaForTesting(
      [&](CompletionOnceCallback completion_callback, int error) {
        new_response = std::make_unique<ResolveHostResponseHelper>(
            resolver_->CreateRequest(
                HostPortPair("new", 70), NetLogWithSource(), base::nullopt,
                request_context_.get(), host_cache_.get()));
        std::move(completion_callback).Run(error);
      });

  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("initial", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper evictee_response(
      resolver_->CreateRequest(HostPortPair("evictee", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      std::move(custom_callback));

  // Now one running request ("initial") and one queued requests ("evictee").
  // Any further requests will cause evictions.
  ResolveHostResponseHelper evictor_response(resolver_->CreateRequest(
      HostPortPair("evictor", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(evictee_response.result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));

  // "new" should evict "evictor"
  EXPECT_THAT(evictor_response.result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));

  proc_->SignalMultiple(2u);

  EXPECT_THAT(initial_response.result_error(), IsOk());
  EXPECT_THAT(new_response->result_error(), IsOk());
}

TEST_F(HostResolverManagerTest, StartWithinEvictionCallback_SameRequest) {
  CreateSerialResolver();
  resolver_->SetMaxQueuedJobsForTesting(2);

  std::unique_ptr<ResolveHostResponseHelper> new_response;
  auto custom_callback = base::BindLambdaForTesting(
      [&](CompletionOnceCallback completion_callback, int error) {
        new_response = std::make_unique<ResolveHostResponseHelper>(
            resolver_->CreateRequest(
                HostPortPair("evictor", 70), NetLogWithSource(), base::nullopt,
                request_context_.get(), host_cache_.get()));
        std::move(completion_callback).Run(error);
      });

  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("initial", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper evictee_response(
      resolver_->CreateRequest(HostPortPair("evictee", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      std::move(custom_callback));
  ResolveHostResponseHelper additional_response(resolver_->CreateRequest(
      HostPortPair("additional", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  // Now one running request ("initial") and two queued requests ("evictee" and
  // "additional"). Any further requests will cause evictions.
  ResolveHostResponseHelper evictor_response(resolver_->CreateRequest(
      HostPortPair("evictor", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(evictee_response.result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));

  // Second "evictor" should be joined with the first and not evict "additional"

  // Only 3 proc requests because both "evictor" requests are combined.
  proc_->SignalMultiple(3u);

  EXPECT_THAT(initial_response.result_error(), IsOk());
  EXPECT_THAT(additional_response.result_error(), IsOk());
  EXPECT_THAT(evictor_response.result_error(), IsOk());
  EXPECT_THAT(new_response->result_error(), IsOk());
}

TEST_F(HostResolverManagerTest, BypassCache) {
  proc_->SignalMultiple(2u);

  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("a", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(initial_response.result_error(), IsOk());
  EXPECT_EQ(1u, proc_->GetCaptureList().size());

  ResolveHostResponseHelper cached_response(resolver_->CreateRequest(
      HostPortPair("a", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(cached_response.result_error(), IsOk());
  // Expect no increase to calls to |proc_| because result was cached.
  EXPECT_EQ(1u, proc_->GetCaptureList().size());

  HostResolver::ResolveHostParameters parameters;
  parameters.cache_usage =
      HostResolver::ResolveHostParameters::CacheUsage::DISALLOWED;
  ResolveHostResponseHelper cache_bypassed_response(resolver_->CreateRequest(
      HostPortPair("a", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(cache_bypassed_response.result_error(), IsOk());
  // Expect call to |proc_| because cache was bypassed.
  EXPECT_EQ(2u, proc_->GetCaptureList().size());
}

// Test that IP address changes flush the cache but initial DNS config reads
// do not.
TEST_F(HostResolverManagerTest, FlushCacheOnIPAddressChange) {
  proc_->SignalMultiple(2u);  // One before the flush, one after.

  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("host1", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(initial_response.result_error(), IsOk());
  EXPECT_EQ(1u, proc_->GetCaptureList().size());

  ResolveHostResponseHelper cached_response(resolver_->CreateRequest(
      HostPortPair("host1", 75), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(cached_response.result_error(), IsOk());
  EXPECT_EQ(1u, proc_->GetCaptureList().size());  // No expected increase.

  // Verify initial DNS config read does not flush cache.
  NetworkChangeNotifier::NotifyObserversOfInitialDNSConfigReadForTests();
  ResolveHostResponseHelper unflushed_response(resolver_->CreateRequest(
      HostPortPair("host1", 75), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(unflushed_response.result_error(), IsOk());
  EXPECT_EQ(1u, proc_->GetCaptureList().size());  // No expected increase.

  // Flush cache by triggering an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  base::RunLoop().RunUntilIdle();  // Notification happens async.

  // Resolve "host1" again -- this time it won't be served from cache, so it
  // will complete asynchronously.
  ResolveHostResponseHelper flushed_response(resolver_->CreateRequest(
      HostPortPair("host1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(flushed_response.result_error(), IsOk());
  EXPECT_EQ(2u, proc_->GetCaptureList().size());  // Expected increase.
}

TEST_F(HostResolverManagerTest, FlushCacheOnDnsConfigChange) {
  proc_->SignalMultiple(2u);  // One before the flush, one after.

  // Resolve to load cache.
  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("host1", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(initial_response.result_error(), IsOk());
  EXPECT_EQ(1u, proc_->GetCaptureList().size());

  // Result expected to come from the cache.
  ResolveHostResponseHelper cached_response(resolver_->CreateRequest(
      HostPortPair("host1", 75), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(cached_response.result_error(), IsOk());
  EXPECT_EQ(1u, proc_->GetCaptureList().size());  // No expected increase.

  // Flush cache by triggering a DNS config change.
  NetworkChangeNotifier::NotifyObserversOfDNSChangeForTests();
  base::RunLoop().RunUntilIdle();  // Notification happens async.

  // Expect flushed from cache and therefore served from |proc_|.
  ResolveHostResponseHelper flushed_response(resolver_->CreateRequest(
      HostPortPair("host1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(flushed_response.result_error(), IsOk());
  EXPECT_EQ(2u, proc_->GetCaptureList().size());  // Expected increase.
}

// Test that IP address changes send ERR_NETWORK_CHANGED to pending requests.
TEST_F(HostResolverManagerTest, AbortOnIPAddressChanged) {
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host1", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  ASSERT_FALSE(response.complete());
  ASSERT_TRUE(proc_->WaitFor(1u));

  // Triggering an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  base::RunLoop().RunUntilIdle();  // Notification happens async.
  proc_->SignalAll();

  EXPECT_THAT(response.result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_EQ(0u, host_cache_->size());
}

// Test that initial DNS config read signals do not abort pending requests.
TEST_F(HostResolverManagerTest, DontAbortOnInitialDNSConfigRead) {
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host1", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  ASSERT_FALSE(response.complete());
  ASSERT_TRUE(proc_->WaitFor(1u));

  // Triggering initial DNS config read signal.
  NetworkChangeNotifier::NotifyObserversOfInitialDNSConfigReadForTests();
  base::RunLoop().RunUntilIdle();  // Notification happens async.
  proc_->SignalAll();

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_TRUE(response.request()->GetAddressResults());
}

// Obey pool constraints after IP address has changed.
TEST_F(HostResolverManagerTest, ObeyPoolConstraintsAfterIPAddressChange) {
  // Runs at most one job at a time.
  CreateSerialResolver();

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("a", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("b", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("c", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  for (auto& response : responses) {
    ASSERT_FALSE(response->complete());
  }
  ASSERT_TRUE(proc_->WaitFor(1u));

  // Triggering an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  base::RunLoop().RunUntilIdle();  // Notification happens async.
  proc_->SignalMultiple(3u);  // Let the false-start go so that we can catch it.

  // Requests should complete one at a time, with the first failing.
  EXPECT_THAT(responses[0]->result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_EQ(1u, num_running_dispatcher_jobs());
  EXPECT_FALSE(responses[1]->complete());
  EXPECT_FALSE(responses[2]->complete());

  EXPECT_THAT(responses[1]->result_error(), IsOk());
  EXPECT_EQ(1u, num_running_dispatcher_jobs());
  EXPECT_FALSE(responses[2]->complete());

  EXPECT_THAT(responses[2]->result_error(), IsOk());
}

// Tests that a new Request made from the callback of a previously aborted one
// will not be aborted.
TEST_F(HostResolverManagerTest, AbortOnlyExistingRequestsOnIPAddressChange) {
  auto custom_callback_template = base::BindLambdaForTesting(
      [&](const HostPortPair& next_host,
          std::unique_ptr<ResolveHostResponseHelper>* next_response,
          CompletionOnceCallback completion_callback, int error) {
        *next_response = std::make_unique<ResolveHostResponseHelper>(
            resolver_->CreateRequest(next_host, NetLogWithSource(),
                                     base::nullopt, request_context_.get(),
                                     host_cache_.get()));
        std::move(completion_callback).Run(error);
      });

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> next_responses(3);

  ResolveHostResponseHelper response0(
      resolver_->CreateRequest(HostPortPair("bbb", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      base::BindOnce(custom_callback_template, HostPortPair("zzz", 80),
                     &next_responses[0]));

  ResolveHostResponseHelper response1(
      resolver_->CreateRequest(HostPortPair("eee", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      base::BindOnce(custom_callback_template, HostPortPair("aaa", 80),
                     &next_responses[1]));

  ResolveHostResponseHelper response2(
      resolver_->CreateRequest(HostPortPair("ccc", 80), NetLogWithSource(),
                               base::nullopt, request_context_.get(),
                               host_cache_.get()),
      base::BindOnce(custom_callback_template, HostPortPair("eee", 80),
                     &next_responses[2]));

  // Wait until all are blocked;
  ASSERT_TRUE(proc_->WaitFor(3u));
  // Trigger an IP address change.
  NetworkChangeNotifier::NotifyObserversOfIPAddressChangeForTests();
  // This should abort all running jobs.
  base::RunLoop().RunUntilIdle();

  EXPECT_THAT(response0.result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_THAT(response1.result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_THAT(response2.result_error(), IsError(ERR_NETWORK_CHANGED));

  EXPECT_FALSE(next_responses[0]->complete());
  EXPECT_FALSE(next_responses[1]->complete());
  EXPECT_FALSE(next_responses[2]->complete());

  // Unblock all calls to proc.
  proc_->SignalMultiple(6u);

  // Run until the re-started requests finish.
  EXPECT_THAT(next_responses[0]->result_error(), IsOk());
  EXPECT_THAT(next_responses[1]->result_error(), IsOk());
  EXPECT_THAT(next_responses[2]->result_error(), IsOk());

  // Verify that results of aborted Jobs were not cached.
  EXPECT_EQ(6u, proc_->GetCaptureList().size());
  EXPECT_EQ(3u, host_cache_->size());
}

// Tests that when the maximum threads is set to 1, requests are dequeued
// in order of priority.
TEST_F(HostResolverManagerTest, HigherPriorityRequestsStartedFirst) {
  CreateSerialResolver();

  HostResolver::ResolveHostParameters low_priority;
  low_priority.initial_priority = LOW;
  HostResolver::ResolveHostParameters medium_priority;
  medium_priority.initial_priority = MEDIUM;
  HostResolver::ResolveHostParameters highest_priority;
  highest_priority.initial_priority = HIGHEST;

  // Note that at this point the MockHostResolverProc is blocked, so any
  // requests we make will not complete.

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req0", 80), NetLogWithSource(), low_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req1", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req2", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req3", 80), NetLogWithSource(), low_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req4", 80), NetLogWithSource(), highest_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req5", 80), NetLogWithSource(), low_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req6", 80), NetLogWithSource(), low_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req5", 80), NetLogWithSource(), highest_priority,
          request_context_.get(), host_cache_.get())));

  for (const auto& response : responses) {
    ASSERT_FALSE(response->complete());
  }

  // Unblock the resolver thread so the requests can run.
  proc_->SignalMultiple(responses.size());  // More than needed.

  // Wait for all the requests to complete successfully.
  for (auto& response : responses) {
    EXPECT_THAT(response->result_error(), IsOk());
  }

  // Since we have restricted to a single concurrent thread in the jobpool,
  // the requests should complete in order of priority (with the exception
  // of the first request, which gets started right away, since there is
  // nothing outstanding).
  MockHostResolverProc::CaptureList capture_list = proc_->GetCaptureList();
  ASSERT_EQ(7u, capture_list.size());

  EXPECT_EQ("req0", capture_list[0].hostname);
  EXPECT_EQ("req4", capture_list[1].hostname);
  EXPECT_EQ("req5", capture_list[2].hostname);
  EXPECT_EQ("req1", capture_list[3].hostname);
  EXPECT_EQ("req2", capture_list[4].hostname);
  EXPECT_EQ("req3", capture_list[5].hostname);
  EXPECT_EQ("req6", capture_list[6].hostname);
}

// Test that changing a job's priority affects the dequeueing order.
TEST_F(HostResolverManagerTest, ChangePriority) {
  CreateSerialResolver();

  HostResolver::ResolveHostParameters lowest_priority;
  lowest_priority.initial_priority = LOWEST;
  HostResolver::ResolveHostParameters low_priority;
  low_priority.initial_priority = LOW;
  HostResolver::ResolveHostParameters medium_priority;
  medium_priority.initial_priority = MEDIUM;

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req0", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req1", 80), NetLogWithSource(), low_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req2", 80), NetLogWithSource(), lowest_priority,
          request_context_.get(), host_cache_.get())));

  // req0 starts immediately; without ChangePriority, req1 and then req2 should
  // run.
  for (const auto& response : responses) {
    ASSERT_FALSE(response->complete());
  }

  // Changing req2 to HIGHEST should make it run before req1.
  // (It can't run before req0, since req0 started immediately.)
  responses[2]->request()->ChangeRequestPriority(HIGHEST);

  // Let all 3 requests finish.
  proc_->SignalMultiple(3u);

  for (auto& response : responses) {
    EXPECT_THAT(response->result_error(), IsOk());
  }

  MockHostResolverProc::CaptureList capture_list = proc_->GetCaptureList();
  ASSERT_EQ(3u, capture_list.size());

  EXPECT_EQ("req0", capture_list[0].hostname);
  EXPECT_EQ("req2", capture_list[1].hostname);
  EXPECT_EQ("req1", capture_list[2].hostname);
}

// Try cancelling a job which has not started yet.
TEST_F(HostResolverManagerTest, CancelPendingRequest) {
  CreateSerialResolver();

  HostResolver::ResolveHostParameters lowest_priority;
  lowest_priority.initial_priority = LOWEST;
  HostResolver::ResolveHostParameters low_priority;
  low_priority.initial_priority = LOW;
  HostResolver::ResolveHostParameters medium_priority;
  medium_priority.initial_priority = MEDIUM;
  HostResolver::ResolveHostParameters highest_priority;
  highest_priority.initial_priority = HIGHEST;

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req0", 80), NetLogWithSource(), lowest_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req1", 80), NetLogWithSource(), highest_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req2", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req3", 80), NetLogWithSource(), low_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req4", 80), NetLogWithSource(), highest_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req5", 80), NetLogWithSource(), lowest_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req6", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));

  // Cancel some requests
  responses[1]->CancelRequest();
  responses[4]->CancelRequest();
  responses[5]->CancelRequest();

  // Unblock the resolver thread so the requests can run.
  proc_->SignalMultiple(responses.size());  // More than needed.

  // Let everything try to finish.
  base::RunLoop().RunUntilIdle();

  // Wait for all the requests to complete succesfully.
  EXPECT_THAT(responses[0]->result_error(), IsOk());
  EXPECT_THAT(responses[2]->result_error(), IsOk());
  EXPECT_THAT(responses[3]->result_error(), IsOk());
  EXPECT_THAT(responses[6]->result_error(), IsOk());

  // Cancelled requests shouldn't complete.
  EXPECT_FALSE(responses[1]->complete());
  EXPECT_FALSE(responses[4]->complete());
  EXPECT_FALSE(responses[5]->complete());

  // Verify that they called out the the resolver proc (which runs on the
  // resolver thread) in the expected order.
  MockHostResolverProc::CaptureList capture_list = proc_->GetCaptureList();
  ASSERT_EQ(4u, capture_list.size());

  EXPECT_EQ("req0", capture_list[0].hostname);
  EXPECT_EQ("req2", capture_list[1].hostname);
  EXPECT_EQ("req6", capture_list[2].hostname);
  EXPECT_EQ("req3", capture_list[3].hostname);
}

// Test that when too many requests are enqueued, old ones start to be aborted.
TEST_F(HostResolverManagerTest, QueueOverflow) {
  CreateSerialResolver();

  // Allow only 3 queued jobs.
  const size_t kMaxPendingJobs = 3u;
  resolver_->SetMaxQueuedJobsForTesting(kMaxPendingJobs);

  HostResolver::ResolveHostParameters lowest_priority;
  lowest_priority.initial_priority = LOWEST;
  HostResolver::ResolveHostParameters low_priority;
  low_priority.initial_priority = LOW;
  HostResolver::ResolveHostParameters medium_priority;
  medium_priority.initial_priority = MEDIUM;
  HostResolver::ResolveHostParameters highest_priority;
  highest_priority.initial_priority = HIGHEST;

  // Note that at this point the MockHostResolverProc is blocked, so any
  // requests we make will not complete.

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req0", 80), NetLogWithSource(), lowest_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req1", 80), NetLogWithSource(), highest_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req2", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req3", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));

  // At this point, there are 3 enqueued jobs (and one "running" job).
  // Insertion of subsequent requests will cause evictions.

  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req4", 80), NetLogWithSource(), low_priority,
          request_context_.get(), host_cache_.get())));
  EXPECT_THAT(responses[4]->result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));  // Evicts self.
  EXPECT_FALSE(responses[4]->request()->GetAddressResults());

  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req5", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));
  EXPECT_THAT(responses[2]->result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));
  EXPECT_FALSE(responses[2]->request()->GetAddressResults());

  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req6", 80), NetLogWithSource(), highest_priority,
          request_context_.get(), host_cache_.get())));
  EXPECT_THAT(responses[3]->result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));
  EXPECT_FALSE(responses[3]->request()->GetAddressResults());

  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("req7", 80), NetLogWithSource(), medium_priority,
          request_context_.get(), host_cache_.get())));
  EXPECT_THAT(responses[5]->result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));
  EXPECT_FALSE(responses[5]->request()->GetAddressResults());

  // Unblock the resolver thread so the requests can run.
  proc_->SignalMultiple(4u);

  // The rest should succeed.
  EXPECT_THAT(responses[0]->result_error(), IsOk());
  EXPECT_TRUE(responses[0]->request()->GetAddressResults());
  EXPECT_THAT(responses[1]->result_error(), IsOk());
  EXPECT_TRUE(responses[1]->request()->GetAddressResults());
  EXPECT_THAT(responses[6]->result_error(), IsOk());
  EXPECT_TRUE(responses[6]->request()->GetAddressResults());
  EXPECT_THAT(responses[7]->result_error(), IsOk());
  EXPECT_TRUE(responses[7]->request()->GetAddressResults());

  // Verify that they called out the the resolver proc (which runs on the
  // resolver thread) in the expected order.
  MockHostResolverProc::CaptureList capture_list = proc_->GetCaptureList();
  ASSERT_EQ(4u, capture_list.size());

  EXPECT_EQ("req0", capture_list[0].hostname);
  EXPECT_EQ("req1", capture_list[1].hostname);
  EXPECT_EQ("req6", capture_list[2].hostname);
  EXPECT_EQ("req7", capture_list[3].hostname);

  // Verify that the evicted (incomplete) requests were not cached.
  EXPECT_EQ(4u, host_cache_->size());

  for (size_t i = 0; i < responses.size(); ++i) {
    EXPECT_TRUE(responses[i]->complete()) << i;
  }
}

// Tests that jobs can self-evict by setting the max queue to 0.
TEST_F(HostResolverManagerTest, QueueOverflow_SelfEvict) {
  CreateSerialResolver();
  resolver_->SetMaxQueuedJobsForTesting(0);

  // Note that at this point the MockHostResolverProc is blocked, so any
  // requests we make will not complete.

  ResolveHostResponseHelper run_response(resolver_->CreateRequest(
      HostPortPair("run", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  ResolveHostResponseHelper evict_response(resolver_->CreateRequest(
      HostPortPair("req1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(evict_response.result_error(),
              IsError(ERR_HOST_RESOLVER_QUEUE_TOO_LARGE));
  EXPECT_FALSE(evict_response.request()->GetAddressResults());

  proc_->SignalMultiple(1u);

  EXPECT_THAT(run_response.result_error(), IsOk());
  EXPECT_TRUE(run_response.request()->GetAddressResults());
}

// Make sure that the dns query type parameter is respected when raw IPs are
// passed in.
TEST_F(HostResolverManagerTest, AddressFamilyWithRawIPs) {
  HostResolver::ResolveHostParameters v4_parameters;
  v4_parameters.dns_query_type = DnsQueryType::A;

  HostResolver::ResolveHostParameters v6_parameters;
  v6_parameters.dns_query_type = DnsQueryType::AAAA;

  ResolveHostResponseHelper v4_v4_request(resolver_->CreateRequest(
      HostPortPair("127.0.0.1", 80), NetLogWithSource(), v4_parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v4_v4_request.result_error(), IsOk());
  EXPECT_THAT(v4_v4_request.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));

  ResolveHostResponseHelper v4_v6_request(resolver_->CreateRequest(
      HostPortPair("127.0.0.1", 80), NetLogWithSource(), v6_parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v4_v6_request.result_error(), IsError(ERR_NAME_NOT_RESOLVED));

  ResolveHostResponseHelper v4_unsp_request(resolver_->CreateRequest(
      HostPortPair("127.0.0.1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v4_unsp_request.result_error(), IsOk());
  EXPECT_THAT(
      v4_unsp_request.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("127.0.0.1", 80)));

  ResolveHostResponseHelper v6_v4_request(resolver_->CreateRequest(
      HostPortPair("::1", 80), NetLogWithSource(), v4_parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v6_v4_request.result_error(), IsError(ERR_NAME_NOT_RESOLVED));

  ResolveHostResponseHelper v6_v6_request(resolver_->CreateRequest(
      HostPortPair("::1", 80), NetLogWithSource(), v6_parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v6_v6_request.result_error(), IsOk());
  EXPECT_THAT(v6_v6_request.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::1", 80)));

  ResolveHostResponseHelper v6_unsp_request(resolver_->CreateRequest(
      HostPortPair("::1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(v6_unsp_request.result_error(), IsOk());
  EXPECT_THAT(
      v6_unsp_request.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("::1", 80)));
}

TEST_F(HostResolverManagerTest, LocalOnly_FromCache) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");
  proc_->SignalMultiple(1u);  // Need only one.

  HostResolver::ResolveHostParameters source_none_parameters;
  source_none_parameters.source = HostResolverSource::LOCAL_ONLY;

  // First NONE query expected to complete synchronously with a cache miss.
  ResolveHostResponseHelper cache_miss_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(),
      source_none_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_TRUE(cache_miss_request.complete());
  EXPECT_THAT(cache_miss_request.result_error(), IsError(ERR_DNS_CACHE_MISS));
  EXPECT_FALSE(cache_miss_request.request()->GetAddressResults());
  EXPECT_FALSE(cache_miss_request.request()->GetStaleInfo());

  // Normal query to populate the cache.
  ResolveHostResponseHelper normal_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(normal_request.result_error(), IsOk());
  EXPECT_FALSE(normal_request.request()->GetStaleInfo());

  // Second NONE query expected to complete synchronously with cache hit.
  ResolveHostResponseHelper cache_hit_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(),
      source_none_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_TRUE(cache_hit_request.complete());
  EXPECT_THAT(cache_hit_request.result_error(), IsOk());
  EXPECT_THAT(
      cache_hit_request.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("192.168.1.42", 80)));
  EXPECT_FALSE(cache_hit_request.request()->GetStaleInfo().value().is_stale());
}

TEST_F(HostResolverManagerTest, LocalOnly_StaleEntry) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");
  proc_->SignalMultiple(1u);  // Need only one.

  HostResolver::ResolveHostParameters source_none_parameters;
  source_none_parameters.source = HostResolverSource::LOCAL_ONLY;

  // First NONE query expected to complete synchronously with a cache miss.
  ResolveHostResponseHelper cache_miss_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(),
      source_none_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_TRUE(cache_miss_request.complete());
  EXPECT_THAT(cache_miss_request.result_error(), IsError(ERR_DNS_CACHE_MISS));
  EXPECT_FALSE(cache_miss_request.request()->GetAddressResults());
  EXPECT_FALSE(cache_miss_request.request()->GetStaleInfo());

  // Normal query to populate the cache.
  ResolveHostResponseHelper normal_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(normal_request.result_error(), IsOk());
  EXPECT_FALSE(normal_request.request()->GetStaleInfo());

  MakeCacheStale();

  // Second NONE query still expected to complete synchronously with cache miss.
  ResolveHostResponseHelper stale_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(),
      source_none_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_TRUE(stale_request.complete());
  EXPECT_THAT(stale_request.result_error(), IsError(ERR_DNS_CACHE_MISS));
  EXPECT_FALSE(stale_request.request()->GetAddressResults());
  EXPECT_FALSE(stale_request.request()->GetStaleInfo());
}

TEST_F(HostResolverManagerTest, LocalOnly_FromIp) {
  HostResolver::ResolveHostParameters source_none_parameters;
  source_none_parameters.source = HostResolverSource::LOCAL_ONLY;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("1.2.3.4", 56), NetLogWithSource(), source_none_parameters,
      request_context_.get(), host_cache_.get()));

  // Expected to resolve synchronously.
  EXPECT_TRUE(response.complete());
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("1.2.3.4", 56)));
  EXPECT_FALSE(response.request()->GetStaleInfo());
}

TEST_F(HostResolverManagerTest, LocalOnly_InvalidName) {
  proc_->AddRuleForAllFamilies("foo,bar.com", "192.168.1.42");

  HostResolver::ResolveHostParameters source_none_parameters;
  source_none_parameters.source = HostResolverSource::LOCAL_ONLY;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("foo,bar.com", 57), NetLogWithSource(),
      source_none_parameters, request_context_.get(), host_cache_.get()));

  // Expected to fail synchronously.
  EXPECT_TRUE(response.complete());
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetStaleInfo());
}

TEST_F(HostResolverManagerTest, LocalOnly_InvalidLocalhost) {
  HostResolver::ResolveHostParameters source_none_parameters;
  source_none_parameters.source = HostResolverSource::LOCAL_ONLY;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("foo,bar.localhost", 58), NetLogWithSource(),
      source_none_parameters, request_context_.get(), host_cache_.get()));

  // Expected to fail synchronously.
  EXPECT_TRUE(response.complete());
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetStaleInfo());
}

TEST_F(HostResolverManagerTest, StaleAllowed) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");
  proc_->SignalMultiple(1u);  // Need only one.

  HostResolver::ResolveHostParameters stale_allowed_parameters;
  stale_allowed_parameters.source = HostResolverSource::LOCAL_ONLY;
  stale_allowed_parameters.cache_usage =
      HostResolver::ResolveHostParameters::CacheUsage::STALE_ALLOWED;

  // First query expected to complete synchronously as a cache miss.
  ResolveHostResponseHelper cache_miss_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(),
      stale_allowed_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_TRUE(cache_miss_request.complete());
  EXPECT_THAT(cache_miss_request.result_error(), IsError(ERR_DNS_CACHE_MISS));
  EXPECT_FALSE(cache_miss_request.request()->GetAddressResults());
  EXPECT_FALSE(cache_miss_request.request()->GetStaleInfo());

  // Normal query to populate cache
  ResolveHostResponseHelper normal_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(normal_request.result_error(), IsOk());
  EXPECT_FALSE(normal_request.request()->GetStaleInfo());

  MakeCacheStale();

  // Second NONE query expected to get a stale cache hit.
  ResolveHostResponseHelper stale_request(resolver_->CreateRequest(
      HostPortPair("just.testing", 84), NetLogWithSource(),
      stale_allowed_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_TRUE(stale_request.complete());
  EXPECT_THAT(stale_request.result_error(), IsOk());
  EXPECT_THAT(stale_request.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.42", 84)));
  EXPECT_TRUE(stale_request.request()->GetStaleInfo().value().is_stale());
}

TEST_F(HostResolverManagerTest, StaleAllowed_NonLocal) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.2.42");
  proc_->SignalMultiple(1u);  // Need only one.

  HostResolver::ResolveHostParameters stale_allowed_parameters;
  stale_allowed_parameters.cache_usage =
      HostResolver::ResolveHostParameters::CacheUsage::STALE_ALLOWED;

  // Normal non-local resolves should still work normally with the STALE_ALLOWED
  // parameter, and there should be no stale info.
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 85), NetLogWithSource(),
      stale_allowed_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.2.42", 85)));
  EXPECT_FALSE(response.request()->GetStaleInfo());
}

TEST_F(HostResolverManagerTest, StaleAllowed_FromIp) {
  HostResolver::ResolveHostParameters stale_allowed_parameters;
  stale_allowed_parameters.cache_usage =
      HostResolver::ResolveHostParameters::CacheUsage::STALE_ALLOWED;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("1.2.3.4", 57), NetLogWithSource(), stale_allowed_parameters,
      request_context_.get(), host_cache_.get()));

  // Expected to resolve synchronously without stale info.
  EXPECT_TRUE(response.complete());
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("1.2.3.4", 57)));
  EXPECT_FALSE(response.request()->GetStaleInfo());
}

// TODO(mgersh): add a test case for errors with positive TTL after
// https://crbug.com/115051 is fixed.

// Test the retry attempts simulating host resolver proc that takes too long.
TEST_F(HostResolverManagerTest, MultipleAttempts) {
  // Total number of attempts would be 3 and we want the 3rd attempt to resolve
  // the host. First and second attempt will be forced to wait until they get
  // word that a resolution has completed. The 3rd resolution attempt will try
  // to get done ASAP, and won't wait.
  int kAttemptNumberToResolve = 3;
  int kTotalAttempts = 3;

  // Add a little bit of extra fudge to the delay to allow reasonable
  // flexibility for time > vs >= etc.  We don't need to fail the test if we
  // retry at t=6001 instead of t=6000.
  base::TimeDelta kSleepFudgeFactor = base::TimeDelta::FromMilliseconds(1);

  scoped_refptr<LookupAttemptHostResolverProc> resolver_proc(
      new LookupAttemptHostResolverProc(nullptr, kAttemptNumberToResolve,
                                        kTotalAttempts));

  ProcTaskParams params = DefaultParams(resolver_proc.get());
  base::TimeDelta unresponsive_delay = params.unresponsive_delay;
  int retry_factor = params.retry_factor;

  CreateResolverWithLimitsAndParams(kMaxJobs, params, true /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);

  // Override the current thread task runner, so we can simulate the passage of
  // time and avoid any actual sleeps.
  auto test_task_runner = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
  base::ScopedClosureRunner task_runner_override_scoped_cleanup =
      base::ThreadTaskRunnerHandle::OverrideForTesting(test_task_runner);

  // Resolve "host1".
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host1", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_FALSE(response.complete());

  resolver_proc->WaitForNAttemptsToBeBlocked(1);
  EXPECT_FALSE(response.complete());

  test_task_runner->FastForwardBy(unresponsive_delay + kSleepFudgeFactor);
  resolver_proc->WaitForNAttemptsToBeBlocked(2);
  EXPECT_FALSE(response.complete());

  test_task_runner->FastForwardBy(unresponsive_delay * retry_factor +
                                  kSleepFudgeFactor);

  resolver_proc->WaitForAllAttemptsToFinish();
  test_task_runner->RunUntilIdle();

  // Resolve returns -4 to indicate that 3rd attempt has resolved the host.
  // Since we're using a TestMockTimeTaskRunner, the RunLoop stuff in
  // result_error() will fail if it actually has to wait, but unless there's an
  // error, the result should be immediately ready by this point.
  EXPECT_EQ(-4, response.result_error());

  // We should be done with retries, but make sure none erroneously happen.
  test_task_runner->FastForwardUntilNoTasksRemain();

  EXPECT_EQ(resolver_proc->total_attempts_resolved(), kTotalAttempts);
  EXPECT_EQ(resolver_proc->resolved_attempt_number(), kAttemptNumberToResolve);
}

// If a host resolves to a list that includes 127.0.53.53, this is treated as
// an error. 127.0.53.53 is a localhost address, however it has been given a
// special significance by ICANN to help surface name collision resulting from
// the new gTLDs.
TEST_F(HostResolverManagerTest, NameCollisionIcann) {
  proc_->AddRuleForAllFamilies("single", "127.0.53.53");
  proc_->AddRuleForAllFamilies("multiple", "127.0.0.1,127.0.53.53");
  proc_->AddRuleForAllFamilies("ipv6", "::127.0.53.53");
  proc_->AddRuleForAllFamilies("not_reserved1", "53.53.0.127");
  proc_->AddRuleForAllFamilies("not_reserved2", "127.0.53.54");
  proc_->AddRuleForAllFamilies("not_reserved3", "10.0.53.53");
  proc_->SignalMultiple(6u);

  ResolveHostResponseHelper single_response(resolver_->CreateRequest(
      HostPortPair("single", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(single_response.result_error(),
              IsError(ERR_ICANN_NAME_COLLISION));
  EXPECT_FALSE(single_response.request()->GetAddressResults());

  // ERR_ICANN_NAME_COLLISION is cached like any other error, using a fixed TTL
  // for failed entries from proc-based resolver. That said, the fixed TTL is 0,
  // so it should never be cached.
  const std::pair<const HostCache::Key, HostCache::Entry>* cache_result =
      GetCacheHit(HostCache::Key("single", DnsQueryType::UNSPECIFIED,
                                 0 /* host_resolver_flags */,
                                 HostResolverSource::ANY));
  EXPECT_FALSE(cache_result);

  ResolveHostResponseHelper multiple_response(resolver_->CreateRequest(
      HostPortPair("multiple", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(multiple_response.result_error(),
              IsError(ERR_ICANN_NAME_COLLISION));

  // Resolving an IP literal of 127.0.53.53 however is allowed.
  ResolveHostResponseHelper literal_response(resolver_->CreateRequest(
      HostPortPair("127.0.53.53", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(literal_response.result_error(), IsOk());

  // Moreover the address should not be recognized when embedded in an IPv6
  // address.
  ResolveHostResponseHelper ipv6_response(resolver_->CreateRequest(
      HostPortPair("127.0.53.53", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(ipv6_response.result_error(), IsOk());

  // Try some other IPs which are similar, but NOT an exact match on
  // 127.0.53.53.
  ResolveHostResponseHelper similar_response1(resolver_->CreateRequest(
      HostPortPair("not_reserved1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(similar_response1.result_error(), IsOk());

  ResolveHostResponseHelper similar_response2(resolver_->CreateRequest(
      HostPortPair("not_reserved2", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(similar_response2.result_error(), IsOk());

  ResolveHostResponseHelper similar_response3(resolver_->CreateRequest(
      HostPortPair("not_reserved3", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(similar_response3.result_error(), IsOk());
}

TEST_F(HostResolverManagerTest, IsIPv6Reachable) {
  // The real HostResolverManager is needed since TestHostResolverManager will
  // bypass the IPv6 reachability tests.
  DestroyResolver();
  host_cache_ = nullptr;
  resolver_ = std::make_unique<HostResolverManager>(DefaultOptions(), nullptr);

  // Verify that two consecutive calls return the same value.
  TestNetLog test_net_log;
  NetLogWithSource net_log =
      NetLogWithSource::Make(&test_net_log, NetLogSourceType::NONE);
  bool result1 = IsIPv6Reachable(net_log);
  bool result2 = IsIPv6Reachable(net_log);
  EXPECT_EQ(result1, result2);

  // Filter reachability check events and verify that there are two of them.
  TestNetLogEntry::List event_list;
  test_net_log.GetEntries(&event_list);
  TestNetLogEntry::List probe_event_list;
  for (const auto& event : event_list) {
    if (event.type ==
        NetLogEventType::HOST_RESOLVER_IMPL_IPV6_REACHABILITY_CHECK) {
      probe_event_list.push_back(event);
    }
  }
  ASSERT_EQ(2U, probe_event_list.size());

  // Verify that the first request was not cached and the second one was.
  bool cached;
  EXPECT_TRUE(probe_event_list[0].GetBooleanValue("cached", &cached));
  EXPECT_FALSE(cached);
  EXPECT_TRUE(probe_event_list[1].GetBooleanValue("cached", &cached));
  EXPECT_TRUE(cached);
}

TEST_F(HostResolverManagerTest, IncludeCanonicalName) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42",
                               HOST_RESOLVER_CANONNAME, "canon.name");
  proc_->SignalMultiple(2u);

  HostResolver::ResolveHostParameters parameters;
  parameters.include_canonical_name = true;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response_no_flag(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.42", 80)));
  EXPECT_EQ("canon.name",
            response.request()->GetAddressResults().value().canonical_name());

  EXPECT_THAT(response_no_flag.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
}

TEST_F(HostResolverManagerTest, LoopbackOnly) {
  proc_->AddRuleForAllFamilies("otherlocal", "127.0.0.1",
                               HOST_RESOLVER_LOOPBACK_ONLY);
  proc_->SignalMultiple(2u);

  HostResolver::ResolveHostParameters parameters;
  parameters.loopback_only = true;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("otherlocal", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response_no_flag(resolver_->CreateRequest(
      HostPortPair("otherlocal", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));

  EXPECT_THAT(response_no_flag.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
}

TEST_F(HostResolverManagerTest, IsSpeculative) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");
  proc_->SignalMultiple(1u);

  HostResolver::ResolveHostParameters parameters;
  parameters.is_speculative = true;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());

  ASSERT_EQ(1u, proc_->GetCaptureList().size());
  EXPECT_EQ("just.testing", proc_->GetCaptureList()[0].hostname);

  // Reresolve without the |is_speculative| flag should immediately return from
  // cache.
  ResolveHostResponseHelper response2(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response2.result_error(), IsOk());
  EXPECT_THAT(response2.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.42", 80)));

  EXPECT_EQ("just.testing", proc_->GetCaptureList()[0].hostname);
  EXPECT_EQ(1u, proc_->GetCaptureList().size());  // No increase.
}

// Test that if a Job with multiple requests, each with its own different
// HostCache, completes, the result is cached in all request HostCaches.
TEST_F(HostResolverManagerTest, MultipleCachesForMultipleRequests) {
  proc_->AddRuleForAllFamilies("just.testing", "192.168.1.42");

  std::unique_ptr<HostCache> cache2 = HostCache::CreateDefaultCache();
  resolver_->AddHostCacheInvalidator(cache2->invalidator());

  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("just.testing", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response2(resolver_->CreateRequest(
      HostPortPair("just.testing", 85), NetLogWithSource(), base::nullopt,
      request_context_.get(), cache2.get()));
  ASSERT_EQ(1u, resolver_->num_jobs_for_testing());

  proc_->SignalMultiple(1u);
  EXPECT_THAT(response1.result_error(), IsOk());
  EXPECT_THAT(response2.result_error(), IsOk());

  HostResolver::ResolveHostParameters local_resolve_parameters;
  local_resolve_parameters.source = HostResolverSource::LOCAL_ONLY;

  // Confirm |host_cache_| contains the result.
  ResolveHostResponseHelper cached_response1(resolver_->CreateRequest(
      HostPortPair("just.testing", 81), NetLogWithSource(),
      local_resolve_parameters, request_context_.get(), host_cache_.get()));
  EXPECT_THAT(cached_response1.result_error(), IsOk());
  EXPECT_THAT(
      cached_response1.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("192.168.1.42", 81)));
  EXPECT_TRUE(cached_response1.request()->GetStaleInfo());

  // Confirm |cache2| contains the result.
  ResolveHostResponseHelper cached_response2(resolver_->CreateRequest(
      HostPortPair("just.testing", 82), NetLogWithSource(),
      local_resolve_parameters, request_context_.get(), cache2.get()));
  EXPECT_THAT(cached_response2.result_error(), IsOk());
  EXPECT_THAT(
      cached_response2.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("192.168.1.42", 82)));
  EXPECT_TRUE(cached_response2.request()->GetStaleInfo());

  resolver_->RemoveHostCacheInvalidator(cache2->invalidator());
}

#if BUILDFLAG(ENABLE_MDNS)
const uint8_t kMdnsResponseA[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x01,              // TYPE is A.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x10,  // TTL is 16 (seconds)
    0x00, 0x04,              // RDLENGTH is 4 bytes.
    0x01, 0x02, 0x03, 0x04,  // 1.2.3.4
};

const uint8_t kMdnsResponseA2[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x01,              // TYPE is A.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x10,  // TTL is 16 (seconds)
    0x00, 0x04,              // RDLENGTH is 4 bytes.
    0x05, 0x06, 0x07, 0x08,  // 5.6.7.8
};

const uint8_t kMdnsResponseA2Goodbye[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x01,              // TYPE is A.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x00,  // TTL is 0 (signaling "goodbye" removal of result)
    0x00, 0x04,              // RDLENGTH is 4 bytes.
    0x05, 0x06, 0x07, 0x08,  // 5.6.7.8
};

const uint8_t kMdnsResponseAAAA[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x1C,              // TYPE is AAAA.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x10,  // TTL is 16 (seconds)
    0x00, 0x10,              // RDLENGTH is 16 bytes.

    // 000a:0000:0000:0000:0001:0002:0003:0004
    0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02,
    0x00, 0x03, 0x00, 0x04};

// An MDNS response indicating that the responder owns the hostname, but the
// specific requested type (AAAA) does not exist because the responder only has
// A addresses.
const uint8_t kMdnsResponseNsec[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x2f,              // TYPE is NSEC.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x10,  // TTL is 16 (seconds)
    0x00, 0x06,              // RDLENGTH is 6 bytes.
    0xc0, 0x0c,  // Next Domain Name (always pointer back to name in MDNS)
    0x00,        // Bitmap block number (always 0 in MDNS)
    0x02,        // Bitmap length is 2
    0x00, 0x08   // A type only
};

const uint8_t kMdnsResponseTxt[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x10,              // TYPE is TXT.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x11,  // TTL is 17 (seconds)
    0x00, 0x08,              // RDLENGTH is 8 bytes.

    // "foo"
    0x03, 0x66, 0x6f, 0x6f,
    // "bar"
    0x03, 0x62, 0x61, 0x72};

const uint8_t kMdnsResponsePtr[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x0c,              // TYPE is PTR.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x12,  // TTL is 18 (seconds)
    0x00, 0x09,              // RDLENGTH is 9 bytes.

    // "foo.com."
    0x03, 'f', 'o', 'o', 0x03, 'c', 'o', 'm', 0x00};

const uint8_t kMdnsResponsePtrRoot[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x0c,              // TYPE is PTR.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x13,  // TTL is 19 (seconds)
    0x00, 0x01,              // RDLENGTH is 1 byte.

    // "." (the root domain)
    0x00};

const uint8_t kMdnsResponseSrv[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x21,              // TYPE is SRV.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x13,  // TTL is 19 (seconds)
    0x00, 0x0f,              // RDLENGTH is 15 bytes.

    0x00, 0x05,  // Priority 5
    0x00, 0x01,  // Weight 1
    0x20, 0x49,  // Port 8265

    // "foo.com."
    0x03, 'f', 'o', 'o', 0x03, 'c', 'o', 'm', 0x00};

const uint8_t kMdnsResponseSrvUnrestricted[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "foo bar(A1B2)._ipps._tcp.local"
    0x0d, 'f', 'o', 'o', ' ', 'b', 'a', 'r', '(', 'A', '1', 'B', '2', ')', 0x05,
    '_', 'i', 'p', 'p', 's', 0x04, '_', 't', 'c', 'p', 0x05, 'l', 'o', 'c', 'a',
    'l', 0x00,

    0x00, 0x21,              // TYPE is SRV.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x13,  // TTL is 19 (seconds)
    0x00, 0x0f,              // RDLENGTH is 15 bytes.

    0x00, 0x05,  // Priority 5
    0x00, 0x01,  // Weight 1
    0x20, 0x49,  // Port 8265

    // "foo.com."
    0x03, 'f', 'o', 'o', 0x03, 'c', 'o', 'm', 0x00};

const uint8_t kMdnsResponseSrvUnrestrictedResult[] = {
    // Header
    0x00, 0x00,  // ID is zeroed out
    0x81, 0x80,  // Standard query response, RA, no error
    0x00, 0x00,  // No questions (for simplicity)
    0x00, 0x01,  // 1 RR (answers)
    0x00, 0x00,  // 0 authority RRs
    0x00, 0x00,  // 0 additional RRs

    // "myhello.local."
    0x07, 'm', 'y', 'h', 'e', 'l', 'l', 'o', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00,

    0x00, 0x21,              // TYPE is SRV.
    0x00, 0x01,              // CLASS is IN.
    0x00, 0x00, 0x00, 0x13,  // TTL is 19 (seconds)
    0x00, 0x15,              // RDLENGTH is 21 bytes.

    0x00, 0x05,  // Priority 5
    0x00, 0x01,  // Weight 1
    0x20, 0x49,  // Port 8265

    // "foo bar.local"
    0x07, 'f', 'o', 'o', ' ', 'b', 'a', 'r', 0x05, 'l', 'o', 'c', 'a', 'l',
    0x00};

TEST_F(HostResolverManagerTest, Mdns) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(4);

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponseA, sizeof(kMdnsResponseA));
  socket_factory_ptr->SimulateReceive(kMdnsResponseAAAA,
                                      sizeof(kMdnsResponseAAAA));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(
      response.request()->GetAddressResults().value().endpoints(),
      testing::UnorderedElementsAre(
          CreateExpected("1.2.3.4", 80),
          CreateExpected("000a:0000:0000:0000:0001:0002:0003:0004", 80)));
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerTest, Mdns_AaaaOnly) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(2);

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::AAAA;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponseAAAA,
                                      sizeof(kMdnsResponseAAAA));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected(
                  "000a:0000:0000:0000:0001:0002:0003:0004", 80)));
}

TEST_F(HostResolverManagerTest, Mdns_Txt) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(2);

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponseTxt,
                                      sizeof(kMdnsResponseTxt));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_THAT(response.request()->GetTextResults(),
              testing::Optional(testing::ElementsAre("foo", "bar")));
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerTest, Mdns_Ptr) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(2);

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 83), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponsePtr,
                                      sizeof(kMdnsResponsePtr));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_THAT(
      response.request()->GetHostnameResults(),
      testing::Optional(testing::ElementsAre(HostPortPair("foo.com", 83))));
}

TEST_F(HostResolverManagerTest, Mdns_Srv) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(2);

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 83), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponseSrv,
                                      sizeof(kMdnsResponseSrv));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_THAT(
      response.request()->GetHostnameResults(),
      testing::Optional(testing::ElementsAre(HostPortPair("foo.com", 8265))));
}

// Test that we are able to create multicast DNS requests that contain
// characters not permitted in the DNS spec such as spaces and parenthesis.
TEST_F(HostResolverManagerTest, Mdns_Srv_Unrestricted) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("foo bar(A1B2)._ipps._tcp.local", 83), NetLogWithSource(),
      parameters, request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponseSrvUnrestricted,
                                      sizeof(kMdnsResponseSrvUnrestricted));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_THAT(
      response.request()->GetHostnameResults(),
      testing::Optional(testing::ElementsAre(HostPortPair("foo.com", 8265))));
}

// Test that we are able to create multicast DNS requests that contain
// characters not permitted in the DNS spec such as spaces and parenthesis.
TEST_F(HostResolverManagerTest, Mdns_Srv_Result_Unrestricted) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 83), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(
      kMdnsResponseSrvUnrestrictedResult,
      sizeof(kMdnsResponseSrvUnrestrictedResult));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_THAT(response.request()->GetHostnameResults(),
              testing::Optional(
                  testing::ElementsAre(HostPortPair("foo bar.local", 8265))));
}

// Test multicast DNS handling of NSEC responses (used for explicit negative
// response).
TEST_F(HostResolverManagerTest, Mdns_Nsec) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(2);

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::AAAA;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponseNsec,
                                      sizeof(kMdnsResponseNsec));

  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
}

TEST_F(HostResolverManagerTest, Mdns_NoResponse) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(4);

  // Add a little bit of extra fudge to the delay to allow reasonable
  // flexibility for time > vs >= etc.  We don't need to fail the test if we
  // timeout at t=6001 instead of t=6000.
  base::TimeDelta kSleepFudgeFactor = base::TimeDelta::FromMilliseconds(1);

  // Override the current thread task runner, so we can simulate the passage of
  // time to trigger the timeout.
  auto test_task_runner = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
  base::ScopedClosureRunner task_runner_override_scoped_cleanup =
      base::ThreadTaskRunnerHandle::OverrideForTesting(test_task_runner);

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  ASSERT_TRUE(test_task_runner->HasPendingTask());
  test_task_runner->FastForwardBy(MDnsTransaction::kTransactionTimeout +
                                  kSleepFudgeFactor);

  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());

  test_task_runner->FastForwardUntilNoTasksRemain();
}

TEST_F(HostResolverManagerTest, Mdns_WrongType) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(2);

  // Add a little bit of extra fudge to the delay to allow reasonable
  // flexibility for time > vs >= etc.  We don't need to fail the test if we
  // timeout at t=6001 instead of t=6000.
  base::TimeDelta kSleepFudgeFactor = base::TimeDelta::FromMilliseconds(1);

  // Override the current thread task runner, so we can simulate the passage of
  // time to trigger the timeout.
  auto test_task_runner = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
  base::ScopedClosureRunner task_runner_override_scoped_cleanup =
      base::ThreadTaskRunnerHandle::OverrideForTesting(test_task_runner);

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::A;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  // Not the requested type. Should be ignored.
  socket_factory_ptr->SimulateReceive(kMdnsResponseTxt,
                                      sizeof(kMdnsResponseTxt));

  ASSERT_TRUE(test_task_runner->HasPendingTask());
  test_task_runner->FastForwardBy(MDnsTransaction::kTransactionTimeout +
                                  kSleepFudgeFactor);

  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());

  test_task_runner->FastForwardUntilNoTasksRemain();
}

// Test for a request for both A and AAAA results where results only exist for
// one type.
TEST_F(HostResolverManagerTest, Mdns_PartialResults) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(4);

  // Add a little bit of extra fudge to the delay to allow reasonable
  // flexibility for time > vs >= etc.  We don't need to fail the test if we
  // timeout at t=6001 instead of t=6000.
  base::TimeDelta kSleepFudgeFactor = base::TimeDelta::FromMilliseconds(1);

  // Override the current thread task runner, so we can simulate the passage of
  // time to trigger the timeout.
  auto test_task_runner = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
  base::ScopedClosureRunner task_runner_override_scoped_cleanup =
      base::ThreadTaskRunnerHandle::OverrideForTesting(test_task_runner);

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  ASSERT_TRUE(test_task_runner->HasPendingTask());

  socket_factory_ptr->SimulateReceive(kMdnsResponseA, sizeof(kMdnsResponseA));
  test_task_runner->FastForwardBy(MDnsTransaction::kTransactionTimeout +
                                  kSleepFudgeFactor);

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("1.2.3.4", 80)));

  test_task_runner->FastForwardUntilNoTasksRemain();
}

TEST_F(HostResolverManagerTest, Mdns_Cancel) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(4);

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  response.CancelRequest();

  socket_factory_ptr->SimulateReceive(kMdnsResponseA, sizeof(kMdnsResponseA));
  socket_factory_ptr->SimulateReceive(kMdnsResponseAAAA,
                                      sizeof(kMdnsResponseAAAA));

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(response.complete());
}

// Test for a two-transaction query where the first fails to start. The second
// should be cancelled.
TEST_F(HostResolverManagerTest, Mdns_PartialFailure) {
  // Setup a mock MDnsClient where the first transaction will always return
  // |false| immediately on Start(). Second transaction may or may not be
  // created, but if it is, Start() not expected to be called because the
  // overall request should immediately fail.
  auto transaction1 = std::make_unique<MockMDnsTransaction>();
  EXPECT_CALL(*transaction1, Start()).WillOnce(Return(false));
  auto transaction2 = std::make_unique<MockMDnsTransaction>();
  EXPECT_CALL(*transaction2, Start()).Times(0);

  auto client = std::make_unique<MockMDnsClient>();
  EXPECT_CALL(*client, CreateTransaction(_, _, _, _))
      .Times(Between(1, 2))  // Second transaction optionally created.
      .WillOnce(Return(ByMove(std::move(transaction1))))
      .WillOnce(Return(ByMove(std::move(transaction2))));
  EXPECT_CALL(*client, IsListening()).WillRepeatedly(Return(true));
  resolver_->SetMdnsClientForTesting(std::move(client));

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_FAILED));
  EXPECT_FALSE(response.request()->GetAddressResults());
}

TEST_F(HostResolverManagerTest, Mdns_ListenFailure) {
  // Inject an MdnsClient mock that will always fail to start listening.
  auto client = std::make_unique<MockMDnsClient>();
  EXPECT_CALL(*client, StartListening(_)).WillOnce(Return(ERR_FAILED));
  EXPECT_CALL(*client, IsListening()).WillRepeatedly(Return(false));
  resolver_->SetMdnsClientForTesting(std::move(client));

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::MULTICAST_DNS;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_FAILED));
  EXPECT_FALSE(response.request()->GetAddressResults());
}

// Implementation of HostResolver::MdnsListenerDelegate that records all
// received results in maps.
class TestMdnsListenerDelegate : public HostResolver::MdnsListener::Delegate {
 public:
  using UpdateKey =
      std::pair<HostResolver::MdnsListener::Delegate::UpdateType, DnsQueryType>;

  void OnAddressResult(
      HostResolver::MdnsListener::Delegate::UpdateType update_type,
      DnsQueryType result_type,
      IPEndPoint address) override {
    address_results_.insert({{update_type, result_type}, std::move(address)});
  }

  void OnTextResult(
      HostResolver::MdnsListener::Delegate::UpdateType update_type,
      DnsQueryType result_type,
      std::vector<std::string> text_records) override {
    for (auto& text_record : text_records) {
      text_results_.insert(
          {{update_type, result_type}, std::move(text_record)});
    }
  }

  void OnHostnameResult(
      HostResolver::MdnsListener::Delegate::UpdateType update_type,
      DnsQueryType result_type,
      HostPortPair host) override {
    hostname_results_.insert({{update_type, result_type}, std::move(host)});
  }

  void OnUnhandledResult(
      HostResolver::MdnsListener::Delegate::UpdateType update_type,
      DnsQueryType result_type) override {
    unhandled_results_.insert({update_type, result_type});
  }

  const std::multimap<UpdateKey, IPEndPoint>& address_results() {
    return address_results_;
  }

  const std::multimap<UpdateKey, std::string>& text_results() {
    return text_results_;
  }

  const std::multimap<UpdateKey, HostPortPair>& hostname_results() {
    return hostname_results_;
  }

  const std::multiset<UpdateKey>& unhandled_results() {
    return unhandled_results_;
  }

  template <typename T>
  static std::pair<UpdateKey, T> CreateExpectedResult(
      HostResolver::MdnsListener::Delegate::UpdateType update_type,
      DnsQueryType query_type,
      T result) {
    return std::make_pair(std::make_pair(update_type, query_type), result);
  }

 private:
  std::multimap<UpdateKey, IPEndPoint> address_results_;
  std::multimap<UpdateKey, std::string> text_results_;
  std::multimap<UpdateKey, HostPortPair> hostname_results_;
  std::multiset<UpdateKey> unhandled_results_;
};

TEST_F(HostResolverManagerTest, MdnsListener) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  base::SimpleTestClock clock;
  clock.SetNow(base::Time::Now());
  auto cache_cleanup_timer = std::make_unique<base::MockOneShotTimer>();
  auto* cache_cleanup_timer_ptr = cache_cleanup_timer.get();
  auto mdns_client =
      std::make_unique<MDnsClientImpl>(&clock, std::move(cache_cleanup_timer));
  ASSERT_THAT(mdns_client->StartListening(socket_factory.get()), IsOk());
  resolver_->SetMdnsClientForTesting(std::move(mdns_client));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 80),
                                    DnsQueryType::A);

  TestMdnsListenerDelegate delegate;
  ASSERT_THAT(listener->Start(&delegate), IsOk());
  ASSERT_THAT(delegate.address_results(), testing::IsEmpty());

  socket_factory->SimulateReceive(kMdnsResponseA, sizeof(kMdnsResponseA));
  socket_factory->SimulateReceive(kMdnsResponseA2, sizeof(kMdnsResponseA2));
  socket_factory->SimulateReceive(kMdnsResponseA2Goodbye,
                                  sizeof(kMdnsResponseA2Goodbye));

  // Per RFC6762 section 10.1, removals take effect 1 second after receiving the
  // goodbye message.
  clock.Advance(base::TimeDelta::FromSeconds(1));
  cache_cleanup_timer_ptr->Fire();

  // Expect 1 record adding "1.2.3.4", another changing to "5.6.7.8", and a
  // final removing "5.6.7.8".
  EXPECT_THAT(delegate.address_results(),
              testing::ElementsAre(
                  TestMdnsListenerDelegate::CreateExpectedResult(
                      HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
                      DnsQueryType::A, CreateExpected("1.2.3.4", 80)),
                  TestMdnsListenerDelegate::CreateExpectedResult(
                      HostResolver::MdnsListener::Delegate::UpdateType::CHANGED,
                      DnsQueryType::A, CreateExpected("5.6.7.8", 80)),
                  TestMdnsListenerDelegate::CreateExpectedResult(
                      HostResolver::MdnsListener::Delegate::UpdateType::REMOVED,
                      DnsQueryType::A, CreateExpected("5.6.7.8", 80))));

  EXPECT_THAT(delegate.text_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.hostname_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverManagerTest, MdnsListener_StartListenFailure) {
  // Inject an MdnsClient mock that will always fail to start listening.
  auto client = std::make_unique<MockMDnsClient>();
  EXPECT_CALL(*client, StartListening(_)).WillOnce(Return(ERR_TIMED_OUT));
  EXPECT_CALL(*client, IsListening()).WillRepeatedly(Return(false));
  resolver_->SetMdnsClientForTesting(std::move(client));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 80),
                                    DnsQueryType::A);
  TestMdnsListenerDelegate delegate;
  EXPECT_THAT(listener->Start(&delegate), IsError(ERR_TIMED_OUT));
  EXPECT_THAT(delegate.address_results(), testing::IsEmpty());
}

// Test that removal notifications are sent on natural expiration of MDNS
// records.
TEST_F(HostResolverManagerTest, MdnsListener_Expiration) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  base::SimpleTestClock clock;
  clock.SetNow(base::Time::Now());
  auto cache_cleanup_timer = std::make_unique<base::MockOneShotTimer>();
  auto* cache_cleanup_timer_ptr = cache_cleanup_timer.get();
  auto mdns_client =
      std::make_unique<MDnsClientImpl>(&clock, std::move(cache_cleanup_timer));
  ASSERT_THAT(mdns_client->StartListening(socket_factory.get()), IsOk());
  resolver_->SetMdnsClientForTesting(std::move(mdns_client));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 100),
                                    DnsQueryType::A);

  TestMdnsListenerDelegate delegate;
  ASSERT_THAT(listener->Start(&delegate), IsOk());
  ASSERT_THAT(delegate.address_results(), testing::IsEmpty());

  socket_factory->SimulateReceive(kMdnsResponseA, sizeof(kMdnsResponseA));

  EXPECT_THAT(
      delegate.address_results(),
      testing::ElementsAre(TestMdnsListenerDelegate::CreateExpectedResult(
          HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
          DnsQueryType::A, CreateExpected("1.2.3.4", 100))));

  clock.Advance(base::TimeDelta::FromSeconds(16));
  cache_cleanup_timer_ptr->Fire();

  EXPECT_THAT(delegate.address_results(),
              testing::ElementsAre(
                  TestMdnsListenerDelegate::CreateExpectedResult(
                      HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
                      DnsQueryType::A, CreateExpected("1.2.3.4", 100)),
                  TestMdnsListenerDelegate::CreateExpectedResult(
                      HostResolver::MdnsListener::Delegate::UpdateType::REMOVED,
                      DnsQueryType::A, CreateExpected("1.2.3.4", 100))));

  EXPECT_THAT(delegate.text_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.hostname_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverManagerTest, MdnsListener_Txt) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 12),
                                    DnsQueryType::TXT);

  TestMdnsListenerDelegate delegate;
  ASSERT_THAT(listener->Start(&delegate), IsOk());
  ASSERT_THAT(delegate.text_results(), testing::IsEmpty());

  socket_factory_ptr->SimulateReceive(kMdnsResponseTxt,
                                      sizeof(kMdnsResponseTxt));

  EXPECT_THAT(delegate.text_results(),
              testing::ElementsAre(
                  TestMdnsListenerDelegate::CreateExpectedResult(
                      HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
                      DnsQueryType::TXT, "foo"),
                  TestMdnsListenerDelegate::CreateExpectedResult(
                      HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
                      DnsQueryType::TXT, "bar")));

  EXPECT_THAT(delegate.address_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.hostname_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverManagerTest, MdnsListener_Ptr) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 13),
                                    DnsQueryType::PTR);

  TestMdnsListenerDelegate delegate;
  ASSERT_THAT(listener->Start(&delegate), IsOk());
  ASSERT_THAT(delegate.text_results(), testing::IsEmpty());

  socket_factory_ptr->SimulateReceive(kMdnsResponsePtr,
                                      sizeof(kMdnsResponsePtr));

  EXPECT_THAT(
      delegate.hostname_results(),
      testing::ElementsAre(TestMdnsListenerDelegate::CreateExpectedResult(
          HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
          DnsQueryType::PTR, HostPortPair("foo.com", 13))));

  EXPECT_THAT(delegate.address_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.text_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverManagerTest, MdnsListener_Srv) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 14),
                                    DnsQueryType::SRV);

  TestMdnsListenerDelegate delegate;
  ASSERT_THAT(listener->Start(&delegate), IsOk());
  ASSERT_THAT(delegate.text_results(), testing::IsEmpty());

  socket_factory_ptr->SimulateReceive(kMdnsResponseSrv,
                                      sizeof(kMdnsResponseSrv));

  EXPECT_THAT(
      delegate.hostname_results(),
      testing::ElementsAre(TestMdnsListenerDelegate::CreateExpectedResult(
          HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
          DnsQueryType::SRV, HostPortPair("foo.com", 8265))));

  EXPECT_THAT(delegate.address_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.text_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.unhandled_results(), testing::IsEmpty());
}

// Ensure query types we are not listening for do not affect MdnsListener.
TEST_F(HostResolverManagerTest, MdnsListener_NonListeningTypes) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 41),
                                    DnsQueryType::A);

  TestMdnsListenerDelegate delegate;
  ASSERT_THAT(listener->Start(&delegate), IsOk());

  socket_factory_ptr->SimulateReceive(kMdnsResponseAAAA,
                                      sizeof(kMdnsResponseAAAA));

  EXPECT_THAT(delegate.address_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.text_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.hostname_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.unhandled_results(), testing::IsEmpty());
}

TEST_F(HostResolverManagerTest, MdnsListener_RootDomain) {
  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));

  std::unique_ptr<HostResolver::MdnsListener> listener =
      resolver_->CreateMdnsListener(HostPortPair("myhello.local", 5),
                                    DnsQueryType::PTR);

  TestMdnsListenerDelegate delegate;
  ASSERT_THAT(listener->Start(&delegate), IsOk());

  socket_factory_ptr->SimulateReceive(kMdnsResponsePtrRoot,
                                      sizeof(kMdnsResponsePtrRoot));

  EXPECT_THAT(delegate.unhandled_results(),
              testing::ElementsAre(std::make_pair(
                  HostResolver::MdnsListener::Delegate::UpdateType::ADDED,
                  DnsQueryType::PTR)));

  EXPECT_THAT(delegate.address_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.text_results(), testing::IsEmpty());
  EXPECT_THAT(delegate.hostname_results(), testing::IsEmpty());
}
#endif  // BUILDFLAG(ENABLE_MDNS)

DnsConfig CreateValidDnsConfig() {
  IPAddress dns_ip(192, 168, 1, 0);
  DnsConfig config;
  config.nameservers.push_back(IPEndPoint(dns_ip, dns_protocol::kDefaultPort));
  EXPECT_TRUE(config.IsValid());
  return config;
}

// Specialized fixture for tests of DnsTask.
class HostResolverManagerDnsTest : public HostResolverManagerTest {
 public:
  HostResolverManagerDnsTest() : dns_client_(nullptr) {}

 protected:
  void TearDown() override {
    HostResolverManagerTest::TearDown();
    ChangeDnsConfig(DnsConfig());
  }

  // HostResolverManagerTest implementation:
  HostResolver::ManagerOptions DefaultOptions() override {
    HostResolver::ManagerOptions options =
        HostResolverManagerTest::DefaultOptions();
    options.dns_client_enabled = true;
    return options;
  }

  // Implements HostResolverManager::DnsClientFactory to create a MockDnsClient
  // with empty config and default rules.
  std::unique_ptr<DnsClient> CreateMockClient(NetLog* net_log) {
    auto dns_client =
        std::make_unique<MockDnsClient>(DnsConfig(), CreateDefaultDnsRules());
    dns_client_ = dns_client.get();
    return dns_client;
  }

  void CreateResolverWithOptionsAndParams(HostResolver::ManagerOptions options,
                                          const ProcTaskParams& params,
                                          bool ipv6_reachable) override {
    DestroyResolver();

    resolver_ = std::make_unique<TestHostResolverManager>(
        options, nullptr /* net_log */, ipv6_reachable,
        base::BindRepeating(&HostResolverManagerDnsTest::CreateMockClient,
                            base::Unretained(this)));
    resolver_->set_proc_params_for_test(params);

    if (host_cache_)
      resolver_->AddHostCacheInvalidator(host_cache_->invalidator());
  }

  // Call after CreateResolver() to update the resolver with a new MockDnsClient
  // using |config| and |rules|.
  void UseMockDnsClient(const DnsConfig& config, MockDnsClientRuleList rules) {
    // HostResolver expects DnsConfig to get set after setting DnsClient, so
    // create first with an empty config and then update the config.
    auto dns_client =
        std::make_unique<MockDnsClient>(DnsConfig(), std::move(rules));
    dns_client_ = dns_client.get();
    resolver_->SetDnsClientForTesting(std::move(dns_client));
    if (!config.Equals(DnsConfig()))
      ChangeDnsConfig(config);
  }

  static MockDnsClientRuleList CreateDefaultDnsRules() {
    MockDnsClientRuleList rules;

    AddDnsRule(&rules, "nodomain", dns_protocol::kTypeA,
               MockDnsClientRule::NODOMAIN, false /* delay */);
    AddDnsRule(&rules, "nodomain", dns_protocol::kTypeAAAA,
               MockDnsClientRule::NODOMAIN, false /* delay */);
    AddDnsRule(&rules, "nx", dns_protocol::kTypeA, MockDnsClientRule::FAIL,
               false /* delay */);
    AddDnsRule(&rules, "nx", dns_protocol::kTypeAAAA, MockDnsClientRule::FAIL,
               false /* delay */);
    AddDnsRule(&rules, "ok", dns_protocol::kTypeA, MockDnsClientRule::OK,
               false /* delay */);
    AddDnsRule(&rules, "ok", dns_protocol::kTypeAAAA, MockDnsClientRule::OK,
               false /* delay */);
    AddDnsRule(&rules, "4ok", dns_protocol::kTypeA, MockDnsClientRule::OK,
               false /* delay */);
    AddDnsRule(&rules, "4ok", dns_protocol::kTypeAAAA, MockDnsClientRule::EMPTY,
               false /* delay */);
    AddDnsRule(&rules, "6ok", dns_protocol::kTypeA, MockDnsClientRule::EMPTY,
               false /* delay */);
    AddDnsRule(&rules, "6ok", dns_protocol::kTypeAAAA, MockDnsClientRule::OK,
               false /* delay */);
    AddDnsRule(&rules, "4nx", dns_protocol::kTypeA, MockDnsClientRule::OK,
               false /* delay */);
    AddDnsRule(&rules, "4nx", dns_protocol::kTypeAAAA, MockDnsClientRule::FAIL,
               false /* delay */);
    AddDnsRule(&rules, "empty", dns_protocol::kTypeA, MockDnsClientRule::EMPTY,
               false /* delay */);
    AddDnsRule(&rules, "empty", dns_protocol::kTypeAAAA,
               MockDnsClientRule::EMPTY, false /* delay */);

    AddDnsRule(&rules, "slow_nx", dns_protocol::kTypeA, MockDnsClientRule::FAIL,
               true /* delay */);
    AddDnsRule(&rules, "slow_nx", dns_protocol::kTypeAAAA,
               MockDnsClientRule::FAIL, true /* delay */);

    AddDnsRule(&rules, "4slow_ok", dns_protocol::kTypeA, MockDnsClientRule::OK,
               true /* delay */);
    AddDnsRule(&rules, "4slow_ok", dns_protocol::kTypeAAAA,
               MockDnsClientRule::OK, false /* delay */);
    AddDnsRule(&rules, "6slow_ok", dns_protocol::kTypeA, MockDnsClientRule::OK,
               false /* delay */);
    AddDnsRule(&rules, "6slow_ok", dns_protocol::kTypeAAAA,
               MockDnsClientRule::OK, true /* delay */);
    AddDnsRule(&rules, "4slow_4ok", dns_protocol::kTypeA, MockDnsClientRule::OK,
               true /* delay */);
    AddDnsRule(&rules, "4slow_4ok", dns_protocol::kTypeAAAA,
               MockDnsClientRule::EMPTY, false /* delay */);
    AddDnsRule(&rules, "4slow_4timeout", dns_protocol::kTypeA,
               MockDnsClientRule::TIMEOUT, true /* delay */);
    AddDnsRule(&rules, "4slow_4timeout", dns_protocol::kTypeAAAA,
               MockDnsClientRule::OK, false /* delay */);
    AddDnsRule(&rules, "4slow_6timeout", dns_protocol::kTypeA,
               MockDnsClientRule::OK, true /* delay */);
    AddDnsRule(&rules, "4slow_6timeout", dns_protocol::kTypeAAAA,
               MockDnsClientRule::TIMEOUT, false /* delay */);

    AddDnsRule(&rules, "4collision", dns_protocol::kTypeA,
               IPAddress(127, 0, 53, 53), false /* delay */);
    AddDnsRule(&rules, "4collision", dns_protocol::kTypeAAAA,
               MockDnsClientRule::EMPTY, false /* delay */);
    AddDnsRule(&rules, "6collision", dns_protocol::kTypeA,
               MockDnsClientRule::EMPTY, false /* delay */);
    // This isn't the expected IP for collisions (but looks close to it).
    AddDnsRule(&rules, "6collision", dns_protocol::kTypeAAAA,
               IPAddress(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 0, 53, 53),
               false /* delay */);

    return rules;
  }

  // Adds a rule to |rules|.
  static void AddDnsRule(MockDnsClientRuleList* rules,
                         const std::string& prefix,
                         uint16_t qtype,
                         MockDnsClientRule::ResultType result_type,
                         bool delay) {
    rules->emplace_back(prefix, qtype, DnsConfig::SecureDnsMode::AUTOMATIC,
                        MockDnsClientRule::Result(result_type), delay);
  }

  static void AddDnsRule(MockDnsClientRuleList* rules,
                         const std::string& prefix,
                         uint16_t qtype,
                         const IPAddress& result_ip,
                         bool delay) {
    rules->emplace_back(prefix, qtype, DnsConfig::SecureDnsMode::AUTOMATIC,
                        MockDnsClientRule::Result(
                            BuildTestDnsResponse(prefix, std::move(result_ip))),
                        delay);
  }

  static void AddDnsRule(MockDnsClientRuleList* rules,
                         const std::string& prefix,
                         uint16_t qtype,
                         IPAddress result_ip,
                         std::string cannonname,
                         bool delay) {
    rules->emplace_back(
        prefix, qtype, DnsConfig::SecureDnsMode::AUTOMATIC,
        MockDnsClientRule::Result(BuildTestDnsResponseWithCname(
            prefix, std::move(result_ip), std::move(cannonname))),
        delay);
  }

  static void AddSecureDnsRule(MockDnsClientRuleList* rules,
                               const std::string& prefix,
                               uint16_t qtype,
                               MockDnsClientRule::ResultType result_type,
                               bool delay) {
    MockDnsClientRule::Result result(result_type);
    result.secure = true;
    rules->emplace_back(prefix, qtype, DnsConfig::SecureDnsMode::AUTOMATIC,
                        std::move(result), delay);
  }

  void ChangeDnsConfig(const DnsConfig& config) {
    NetworkChangeNotifier::SetDnsConfig(config);
    // Notification is delivered asynchronously.
    base::RunLoop().RunUntilIdle();
  }

  void SetInitialDnsConfig(const DnsConfig& config) {
    NetworkChangeNotifier::ClearDnsConfigForTesting();
    NetworkChangeNotifier::SetDnsConfig(config);
    // Notification is delivered asynchronously.
    base::RunLoop().RunUntilIdle();
  }

  // Owned by |resolver_|.
  MockDnsClient* dns_client_;
};

TEST_F(HostResolverManagerDnsTest, DisableAndEnableDnsClient) {
  // Disable fallback to allow testing how requests are initially handled.
  set_allow_fallback_to_proctask(false);

  ChangeDnsConfig(CreateValidDnsConfig());
  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.2.47");
  proc_->SignalMultiple(1u);

  resolver_->SetDnsClientEnabled(false);
  ResolveHostResponseHelper response_proc(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 1212), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_proc.result_error(), IsOk());
  EXPECT_THAT(response_proc.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.2.47", 1212)));

  resolver_->SetDnsClientEnabled(true);
  ResolveHostResponseHelper response_dns_client(resolver_->CreateRequest(
      HostPortPair("ok_fail", 1212), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_dns_client.result_error(), IsOk());
  EXPECT_THAT(
      response_dns_client.request()->GetAddressResults().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpected("::1", 1212),
                                    CreateExpected("127.0.0.1", 1212)));
}

// RFC 6761 localhost names should always resolve to loopback.
TEST_F(HostResolverManagerDnsTest, LocalhostLookup) {
  // Add a rule resolving localhost names to a non-loopback IP and test
  // that they still resolves to loopback.
  proc_->AddRuleForAllFamilies("foo.localhost", "192.168.1.42");
  proc_->AddRuleForAllFamilies("localhost", "192.168.1.42");
  proc_->AddRuleForAllFamilies("localhost.", "192.168.1.42");

  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("foo.localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response0.result_error(), IsOk());
  EXPECT_THAT(response0.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response1.result_error(), IsOk());
  EXPECT_THAT(response1.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  ResolveHostResponseHelper response2(resolver_->CreateRequest(
      HostPortPair("localhost.", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response2.result_error(), IsOk());
  EXPECT_THAT(response2.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
}

// RFC 6761 localhost names should always resolve to loopback, even if a HOSTS
// file is active.
TEST_F(HostResolverManagerDnsTest, LocalhostLookupWithHosts) {
  DnsHosts hosts;
  hosts[DnsHostsKey("localhost", ADDRESS_FAMILY_IPV4)] =
      IPAddress({192, 168, 1, 1});
  hosts[DnsHostsKey("foo.localhost", ADDRESS_FAMILY_IPV4)] =
      IPAddress({192, 168, 1, 2});

  DnsConfig config = CreateValidDnsConfig();
  config.hosts = hosts;
  ChangeDnsConfig(config);

  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response0.result_error(), IsOk());
  EXPECT_THAT(response0.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("foo.localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response1.result_error(), IsOk());
  EXPECT_THAT(response1.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
}

// Test successful and fallback resolutions in HostResolverManager::DnsTask.
TEST_F(HostResolverManagerDnsTest, DnsTask) {
  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.1.102");
  // All other hostnames will fail in proc_.

  // Initially there is no config, so client should not be invoked.
  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("ok_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_FALSE(initial_response.complete());

  proc_->SignalMultiple(1u);

  EXPECT_THAT(initial_response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));

  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("ok_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("nx_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response2(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  proc_->SignalMultiple(4u);

  // Resolved by MockDnsClient.
  EXPECT_THAT(response0.result_error(), IsOk());
  EXPECT_THAT(response0.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  // Fallback to ProcTask.
  EXPECT_THAT(response1.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_THAT(response2.result_error(), IsOk());
  EXPECT_THAT(response2.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.102", 80)));
}

// Test successful and failing resolutions in HostResolverManager::DnsTask when
// fallback to ProcTask is disabled.
TEST_F(HostResolverManagerDnsTest, NoFallbackToProcTask) {
  set_allow_fallback_to_proctask(false);

  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.1.102");
  // All other hostnames will fail in proc_.

  // Set empty DnsConfig.
  ChangeDnsConfig(DnsConfig());
  // Initially there is no config, so client should not be invoked.
  ResolveHostResponseHelper initial_response0(resolver_->CreateRequest(
      HostPortPair("ok_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper initial_response1(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  proc_->SignalMultiple(2u);

  EXPECT_THAT(initial_response0.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_THAT(initial_response1.result_error(), IsOk());
  EXPECT_THAT(
      initial_response1.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("192.168.1.102", 80)));

  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper abort_response0(resolver_->CreateRequest(
      HostPortPair("ok_abort", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper abort_response1(resolver_->CreateRequest(
      HostPortPair("nx_abort", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  // Simulate the case when the preference or policy has disabled the DNS
  // client causing AbortDnsTasks.
  UseMockDnsClient(CreateValidDnsConfig(), CreateDefaultDnsRules());

  // First request is resolved by MockDnsClient, others should fail due to
  // disabled fallback to ProcTask.
  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("ok_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("nx_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  proc_->SignalMultiple(6u);

  // Aborted due to Network Change.
  EXPECT_THAT(abort_response0.result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_THAT(abort_response1.result_error(), IsError(ERR_NETWORK_CHANGED));
  // Resolved by MockDnsClient.
  EXPECT_THAT(response0.result_error(), IsOk());
  EXPECT_THAT(response0.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
  // Fallback to ProcTask is disabled.
  EXPECT_THAT(response1.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
}

// Test behavior of OnDnsTaskFailure when Job is aborted.
TEST_F(HostResolverManagerDnsTest, OnDnsTaskFailureAbortedJob) {
  ChangeDnsConfig(CreateValidDnsConfig());
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("nx_abort", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  // Abort all jobs here.
  CreateResolver();
  proc_->SignalMultiple(1u);
  // Run to completion.
  base::RunLoop().RunUntilIdle();  // Notification happens async.
  // It shouldn't crash during OnDnsTaskFailure callbacks.
  EXPECT_FALSE(response.complete());

  // Repeat test with Fallback to ProcTask disabled
  set_allow_fallback_to_proctask(false);
  ChangeDnsConfig(CreateValidDnsConfig());
  ResolveHostResponseHelper no_fallback_response(resolver_->CreateRequest(
      HostPortPair("nx_abort", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  // Abort all jobs here.
  CreateResolver();
  proc_->SignalMultiple(2u);
  // Run to completion.
  base::RunLoop().RunUntilIdle();  // Notification happens async.
  // It shouldn't crash during OnDnsTaskFailure callbacks.
  EXPECT_FALSE(no_fallback_response.complete());
}

// Fallback to proc allowed with ANY source.
TEST_F(HostResolverManagerDnsTest, FallbackBySource_Any) {
  // Ensure fallback is otherwise allowed by resolver settings.
  set_allow_fallback_to_proctask(true);

  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.1.102");
  // All other hostnames will fail in proc_.

  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("nx_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  proc_->SignalMultiple(2u);

  EXPECT_THAT(response0.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_THAT(response1.result_error(), IsOk());
  EXPECT_THAT(response1.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.102", 80)));
}

// Fallback to proc not allowed with DNS source.
TEST_F(HostResolverManagerDnsTest, FallbackBySource_Dns) {
  // Ensure fallback is otherwise allowed by resolver settings.
  set_allow_fallback_to_proctask(true);

  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.1.102");
  // All other hostnames will fail in proc_.

  ChangeDnsConfig(CreateValidDnsConfig());

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("nx_fail", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  // Nothing should reach |proc_| on success, but let failures through to fail
  // instead of hanging.
  proc_->SignalMultiple(2u);

  EXPECT_THAT(response0.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_THAT(response1.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
}

// Fallback to proc on DnsClient change allowed with ANY source.
TEST_F(HostResolverManagerDnsTest, FallbackOnAbortBySource_Any) {
  // Ensure fallback is otherwise allowed by resolver settings.
  set_allow_fallback_to_proctask(true);

  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.1.102");
  // All other hostnames will fail in proc_.

  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("ok_fail", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  proc_->SignalMultiple(2u);

  // Simulate the case when the preference or policy has disabled the DNS client
  // causing AbortDnsTasks.
  resolver_->SetDnsClientEnabled(false);

  // All requests should fallback to proc resolver.
  EXPECT_THAT(response0.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_THAT(response1.result_error(), IsOk());
  EXPECT_THAT(response1.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.102", 80)));
}

// Fallback to proc on DnsClient change not allowed with DNS source.
TEST_F(HostResolverManagerDnsTest, FallbackOnAbortBySource_Dns) {
  // Ensure fallback is otherwise allowed by resolver settings.
  set_allow_fallback_to_proctask(true);

  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.1.102");
  // All other hostnames will fail in proc_.

  ChangeDnsConfig(CreateValidDnsConfig());

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("ok_fail", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  // Nothing should reach |proc_| on success, but let failures through to fail
  // instead of hanging.
  proc_->SignalMultiple(2u);

  // Simulate the case when the preference or policy has disabled the DNS client
  // causing AbortDnsTasks.
  resolver_->SetDnsClientEnabled(false);

  // No fallback expected.  All requests should fail.
  EXPECT_THAT(response0.result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_THAT(response1.result_error(), IsError(ERR_NETWORK_CHANGED));
}

TEST_F(HostResolverManagerDnsTest, DnsTaskUnspec) {
  ChangeDnsConfig(CreateValidDnsConfig());

  proc_->AddRuleForAllFamilies("4nx", "192.168.1.101");
  // All other hostnames will fail in proc_.

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("6ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4nx", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  proc_->SignalMultiple(4u);

  for (auto& response : responses) {
    EXPECT_THAT(response->result_error(), IsOk());
  }

  EXPECT_THAT(responses[0]->request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
  EXPECT_THAT(responses[1]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));
  EXPECT_THAT(responses[2]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::1", 80)));
  EXPECT_THAT(responses[3]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.1.101", 80)));
}

TEST_F(HostResolverManagerDnsTest, NameCollisionIcann) {
  ChangeDnsConfig(CreateValidDnsConfig());

  // When the resolver returns an A record with 127.0.53.53 it should be
  // mapped to a special error.
  ResolveHostResponseHelper response_ipv4(resolver_->CreateRequest(
      HostPortPair("4collision", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_ipv4.result_error(), IsError(ERR_ICANN_NAME_COLLISION));
  EXPECT_FALSE(response_ipv4.request()->GetAddressResults());

  // When the resolver returns an AAAA record with ::127.0.53.53 it should
  // work just like any other IP. (Despite having the same suffix, it is not
  // considered special)
  ResolveHostResponseHelper response_ipv6(resolver_->CreateRequest(
      HostPortPair("6collision", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_ipv6.result_error(), IsOk());
  EXPECT_THAT(response_ipv6.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::127.0.53.53", 80)));
}

TEST_F(HostResolverManagerDnsTest, ServeFromHosts) {
  // Initially, use empty HOSTS file.
  DnsConfig config = CreateValidDnsConfig();
  ChangeDnsConfig(config);

  proc_->AddRuleForAllFamilies(std::string(),
                               std::string());  // Default to failures.
  proc_->SignalMultiple(1u);  // For the first request which misses.

  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("nx_ipv4", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(initial_response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));

  IPAddress local_ipv4 = IPAddress::IPv4Localhost();
  IPAddress local_ipv6 = IPAddress::IPv6Localhost();

  DnsHosts hosts;
  hosts[DnsHostsKey("nx_ipv4", ADDRESS_FAMILY_IPV4)] = local_ipv4;
  hosts[DnsHostsKey("nx_ipv6", ADDRESS_FAMILY_IPV6)] = local_ipv6;
  hosts[DnsHostsKey("nx_both", ADDRESS_FAMILY_IPV4)] = local_ipv4;
  hosts[DnsHostsKey("nx_both", ADDRESS_FAMILY_IPV6)] = local_ipv6;

  // Update HOSTS file.
  config.hosts = hosts;
  ChangeDnsConfig(config);

  ResolveHostResponseHelper response_ipv4(resolver_->CreateRequest(
      HostPortPair("nx_ipv4", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_ipv4.result_error(), IsOk());
  EXPECT_THAT(response_ipv4.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));

  ResolveHostResponseHelper response_ipv6(resolver_->CreateRequest(
      HostPortPair("nx_ipv6", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_ipv6.result_error(), IsOk());
  EXPECT_THAT(response_ipv6.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::1", 80)));

  ResolveHostResponseHelper response_both(resolver_->CreateRequest(
      HostPortPair("nx_both", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_both.result_error(), IsOk());
  EXPECT_THAT(response_both.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  // Requests with specified DNS query type.
  HostResolver::ResolveHostParameters parameters;

  parameters.dns_query_type = DnsQueryType::A;
  ResolveHostResponseHelper response_specified_ipv4(resolver_->CreateRequest(
      HostPortPair("nx_ipv4", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_specified_ipv4.result_error(), IsOk());
  EXPECT_THAT(response_specified_ipv4.request()
                  ->GetAddressResults()
                  .value()
                  .endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));

  parameters.dns_query_type = DnsQueryType::AAAA;
  ResolveHostResponseHelper response_specified_ipv6(resolver_->CreateRequest(
      HostPortPair("nx_ipv6", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_specified_ipv6.result_error(), IsOk());
  EXPECT_THAT(response_specified_ipv6.request()
                  ->GetAddressResults()
                  .value()
                  .endpoints(),
              testing::ElementsAre(CreateExpected("::1", 80)));

  // Request with upper case.
  ResolveHostResponseHelper response_upper(resolver_->CreateRequest(
      HostPortPair("nx_IPV4", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response_upper.result_error(), IsOk());
  EXPECT_THAT(response_upper.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));
}

TEST_F(HostResolverManagerDnsTest, CacheHostsLookupOnConfigChange) {
  // Only allow 1 resolution at a time, so that the second lookup is queued and
  // occurs when the DNS config changes.
  CreateResolverWithLimitsAndParams(1u, DefaultParams(proc_.get()),
                                    true /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);
  DnsConfig config = CreateValidDnsConfig();
  ChangeDnsConfig(config);

  proc_->AddRuleForAllFamilies(std::string(),
                               std::string());  // Default to failures.
  proc_->SignalMultiple(1u);  // For the first request which fails.

  ResolveHostResponseHelper failure_response(resolver_->CreateRequest(
      HostPortPair("nx_ipv4", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper queued_response(resolver_->CreateRequest(
      HostPortPair("nx_ipv6", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  DnsHosts hosts;
  hosts[DnsHostsKey("nx_ipv4", ADDRESS_FAMILY_IPV4)] =
      IPAddress::IPv4Localhost();
  hosts[DnsHostsKey("nx_ipv6", ADDRESS_FAMILY_IPV6)] =
      IPAddress::IPv6Localhost();

  config.hosts = hosts;
  ChangeDnsConfig(config);

  EXPECT_THAT(failure_response.result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_THAT(queued_response.result_error(), IsOk());
  EXPECT_THAT(
      queued_response.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("::1", 80)));

  // Resolutions done by consulting the HOSTS file when the DNS config changes
  // should result in a secure cache entry with SOURCE_HOSTS.
  HostCache::Key key =
      HostCache::Key("nx_ipv6", DnsQueryType::UNSPECIFIED,
                     0 /* host_resolver_flags */, HostResolverSource::ANY);
  key.secure = true;
  const std::pair<const HostCache::Key, HostCache::Entry>* cache_result =
      GetCacheHit(key);
  ASSERT_THAT(cache_result, NotNull());
  EXPECT_EQ(HostCache::Entry::SOURCE_HOSTS, cache_result->second.source());
}

// Test that hosts ending in ".local" or ".local." are resolved using the system
// resolver.
TEST_F(HostResolverManagerDnsTest, BypassDnsTask) {
  ChangeDnsConfig(CreateValidDnsConfig());

  proc_->AddRuleForAllFamilies(std::string(),
                               std::string());  // Default to failures.

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;

  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("ok.local", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("ok.local.", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("oklocal", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("oklocal.", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  proc_->SignalMultiple(5u);

  for (size_t i = 0; i < 2; ++i)
    EXPECT_THAT(responses[i]->result_error(), IsError(ERR_NAME_NOT_RESOLVED));

  for (size_t i = 2; i < responses.size(); ++i)
    EXPECT_THAT(responses[i]->result_error(), IsOk());
}

#if BUILDFLAG(ENABLE_MDNS)
// Test that non-address queries for hosts ending in ".local" are resolved using
// the MDNS resolver.
TEST_F(HostResolverManagerDnsTest, BypassDnsToMdnsWithNonAddress) {
  // Ensure DNS task and system (proc) requests will fail.
  MockDnsClientRuleList rules;
  rules.emplace_back("myhello.local", dns_protocol::kTypeTXT,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(MockDnsClientRule::FAIL),
                     false /* delay */);
  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));
  proc_->AddRuleForAllFamilies(std::string(), std::string());

  auto socket_factory = std::make_unique<MockMDnsSocketFactory>();
  MockMDnsSocketFactory* socket_factory_ptr = socket_factory.get();
  resolver_->SetMdnsSocketFactoryForTesting(std::move(socket_factory));
  // 2 socket creations for every transaction.
  EXPECT_CALL(*socket_factory_ptr, OnSendTo(_)).Times(2);

  HostResolver::ResolveHostParameters dns_parameters;
  dns_parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("myhello.local", 80), NetLogWithSource(), dns_parameters,
      request_context_.get(), host_cache_.get()));

  socket_factory_ptr->SimulateReceive(kMdnsResponseTxt,
                                      sizeof(kMdnsResponseTxt));
  proc_->SignalMultiple(1u);

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetTextResults(),
              testing::Optional(testing::ElementsAre("foo", "bar")));
}
#endif  // BUILDFLAG(ENABLE_MDNS)

// Test that DNS task is always used when explicitly requested as the source,
// even with a case that would normally bypass it eg hosts ending in ".local".
TEST_F(HostResolverManagerDnsTest, DnsNotBypassedWhenDnsSource) {
  // Ensure DNS task requests will succeed and system (proc) requests will fail.
  ChangeDnsConfig(CreateValidDnsConfig());
  proc_->AddRuleForAllFamilies(std::string(), std::string());

  HostResolver::ResolveHostParameters dns_parameters;
  dns_parameters.source = HostResolverSource::DNS;

  ResolveHostResponseHelper dns_response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), dns_parameters,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper dns_local_response(resolver_->CreateRequest(
      HostPortPair("ok.local", 80), NetLogWithSource(), dns_parameters,
      request_context_.get(), host_cache_.get()));
  ResolveHostResponseHelper normal_local_response(resolver_->CreateRequest(
      HostPortPair("ok.local", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  proc_->SignalMultiple(3u);

  EXPECT_THAT(dns_response.result_error(), IsOk());
  EXPECT_THAT(dns_local_response.result_error(), IsOk());
  EXPECT_THAT(normal_local_response.result_error(),
              IsError(ERR_NAME_NOT_RESOLVED));
}

TEST_F(HostResolverManagerDnsTest, SystemOnlyBypassesDnsTask) {
  // Ensure DNS task requests will succeed and system (proc) requests will fail.
  ChangeDnsConfig(CreateValidDnsConfig());
  proc_->AddRuleForAllFamilies(std::string(), std::string());

  ResolveHostResponseHelper dns_response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::SYSTEM;
  ResolveHostResponseHelper system_response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  proc_->SignalMultiple(2u);

  EXPECT_THAT(dns_response.result_error(), IsOk());
  EXPECT_THAT(system_response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
}

TEST_F(HostResolverManagerDnsTest, DisableDnsClientOnPersistentFailure) {
  ChangeDnsConfig(CreateValidDnsConfig());

  proc_->AddRuleForAllFamilies(std::string(),
                               std::string());  // Default to failures.

  // Check that DnsTask works.
  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("ok_1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(initial_response.result_error(), IsOk());

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  for (unsigned i = 0; i < maximum_dns_failures(); ++i) {
    // Use custom names to require separate Jobs.
    std::string hostname = base::StringPrintf("nx_%u", i);
    // Ensure fallback to ProcTask succeeds.
    proc_->AddRuleForAllFamilies(hostname, "192.168.1.101");
    responses.emplace_back(
        std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
            HostPortPair(hostname, 80), NetLogWithSource(), base::nullopt,
            request_context_.get(), host_cache_.get())));
  }

  proc_->SignalMultiple(responses.size());

  for (size_t i = 0; i < responses.size(); ++i)
    EXPECT_THAT(responses[i]->result_error(), IsOk());

  ASSERT_FALSE(proc_->HasBlockedRequests());

  // DnsTask should be disabled by now unless explictly requested via |source|.
  ResolveHostResponseHelper fail_response(resolver_->CreateRequest(
      HostPortPair("ok_2", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::DNS;
  ResolveHostResponseHelper dns_response(resolver_->CreateRequest(
      HostPortPair("ok_2", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  proc_->SignalMultiple(2u);
  EXPECT_THAT(fail_response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_THAT(dns_response.result_error(), IsOk());

  // Check that it is re-enabled after DNS change.
  ChangeDnsConfig(CreateValidDnsConfig());
  ResolveHostResponseHelper reenabled_response(resolver_->CreateRequest(
      HostPortPair("ok_3", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(reenabled_response.result_error(), IsOk());
}

TEST_F(HostResolverManagerDnsTest, DontDisableDnsClientOnSporadicFailure) {
  ChangeDnsConfig(CreateValidDnsConfig());

  // |proc_| defaults to successes.

  // 20 failures interleaved with 20 successes.
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  for (unsigned i = 0; i < 40; ++i) {
    // Use custom names to require separate Jobs.
    std::string hostname = (i % 2) == 0 ? base::StringPrintf("nx_%u", i)
                                        : base::StringPrintf("ok_%u", i);
    responses.emplace_back(
        std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
            HostPortPair(hostname, 80), NetLogWithSource(), base::nullopt,
            request_context_.get(), host_cache_.get())));
  }

  proc_->SignalMultiple(40u);

  for (const auto& response : responses)
    EXPECT_THAT(response->result_error(), IsOk());

  // Make |proc_| default to failures.
  proc_->AddRuleForAllFamilies(std::string(), std::string());

  // DnsTask should still be enabled.
  ResolveHostResponseHelper final_response(resolver_->CreateRequest(
      HostPortPair("ok_last", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(final_response.result_error(), IsOk());
}

// Confirm that resolving "localhost" is unrestricted even if there are no
// global IPv6 address. See SystemHostResolverCall for rationale.
// Test both the DnsClient and system host resolver paths.
TEST_F(HostResolverManagerDnsTest, DualFamilyLocalhost) {
  CreateResolverWithLimitsAndParams(kMaxJobs, DefaultParams(proc_.get()),
                                    false /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);

  // Make request fail if we actually get to the system resolver.
  proc_->AddRuleForAllFamilies(std::string(), std::string());

  // Try without DnsClient.
  resolver_->SetDnsClientEnabled(false);
  ResolveHostResponseHelper system_response(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(system_response.result_error(), IsOk());
  EXPECT_THAT(
      system_response.request()->GetAddressResults().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                    CreateExpected("::1", 80)));

  // With DnsClient
  UseMockDnsClient(CreateValidDnsConfig(), CreateDefaultDnsRules());
  ResolveHostResponseHelper builtin_response(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(builtin_response.result_error(), IsOk());
  EXPECT_THAT(
      builtin_response.request()->GetAddressResults().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                    CreateExpected("::1", 80)));

  // DnsClient configured without ipv6 (but ipv6 should still work for
  // localhost).
  DnsConfig config = CreateValidDnsConfig();
  config.use_local_ipv6 = false;
  ChangeDnsConfig(config);
  ResolveHostResponseHelper ipv6_disabled_response(resolver_->CreateRequest(
      HostPortPair("localhost", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(ipv6_disabled_response.result_error(), IsOk());
  EXPECT_THAT(
      ipv6_disabled_response.request()->GetAddressResults().value().endpoints(),
      testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                    CreateExpected("::1", 80)));
}

// Cancel a request with a single DNS transaction active.
TEST_F(HostResolverManagerDnsTest, CancelWithOneTransactionActive) {
  // Disable ipv6 to ensure we'll only try a single transaction for the host.
  CreateResolverWithLimitsAndParams(kMaxJobs, DefaultParams(proc_.get()),
                                    false /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);
  DnsConfig config = CreateValidDnsConfig();
  config.use_local_ipv6 = false;
  ChangeDnsConfig(config);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());
  ASSERT_EQ(1u, num_running_dispatcher_jobs());

  response.CancelRequest();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(response.complete());

  // Dispatcher state checked in TearDown.
}

// Cancel a request with a single DNS transaction active and another pending.
TEST_F(HostResolverManagerDnsTest, CancelWithOneTransactionActiveOnePending) {
  CreateSerialResolver();
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(1u, num_running_dispatcher_jobs());

  response.CancelRequest();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(response.complete());

  // Dispatcher state checked in TearDown.
}

// Cancel a request with two DNS transactions active.
TEST_F(HostResolverManagerDnsTest, CancelWithTwoTransactionsActive) {
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(2u, num_running_dispatcher_jobs());

  response.CancelRequest();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(response.complete());

  // Dispatcher state checked in TearDown.
}

// Delete a resolver with some active requests and some queued requests.
TEST_F(HostResolverManagerDnsTest, DeleteWithActiveTransactions) {
  // At most 10 Jobs active at once.
  CreateResolverWithLimitsAndParams(10u, DefaultParams(proc_.get()),
                                    true /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);

  ChangeDnsConfig(CreateValidDnsConfig());

  // Add 12 DNS lookups (creating well more than 10 transaction).
  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  for (int i = 0; i < 12; ++i) {
    std::string hostname = base::StringPrintf("ok%i", i);
    responses.emplace_back(
        std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
            HostPortPair(hostname, 80), NetLogWithSource(), base::nullopt,
            request_context_.get(), host_cache_.get())));
  }
  EXPECT_EQ(10u, num_running_dispatcher_jobs());

  DestroyResolver();

  base::RunLoop().RunUntilIdle();
  for (auto& response : responses) {
    EXPECT_FALSE(response->complete());
  }
}

TEST_F(HostResolverManagerDnsTest, DeleteWithCompletedRequests) {
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  DestroyResolver();

  // Completed requests should be unaffected by manager destruction.
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
}

TEST_F(HostResolverManagerDnsTest, ExplicitCancel) {
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_4ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  response.request()->Cancel();
  dns_client_->CompleteDelayedTransactions();

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(response.complete());
}

TEST_F(HostResolverManagerDnsTest, ExplicitCancel_Completed) {
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  response.request()->Cancel();

  // Completed requests should be unaffected by cancellation.
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
}

// Cancel a request with only the IPv6 transaction active.
TEST_F(HostResolverManagerDnsTest, CancelWithIPv6TransactionActive) {
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("6slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(2u, num_running_dispatcher_jobs());

  // The IPv4 request should complete, the IPv6 request is still pending.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, num_running_dispatcher_jobs());

  response.CancelRequest();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(response.complete());

  // Dispatcher state checked in TearDown.
}

// Cancel a request with only the IPv4 transaction pending.
TEST_F(HostResolverManagerDnsTest, CancelWithIPv4TransactionPending) {
  set_allow_fallback_to_proctask(false);
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(2u, num_running_dispatcher_jobs());

  // The IPv6 request should complete, the IPv4 request is still pending.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, num_running_dispatcher_jobs());

  response.CancelRequest();
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(response.complete());
}

// Test cases where AAAA completes first.
TEST_F(HostResolverManagerDnsTest, AAAACompletesFirst) {
  set_allow_fallback_to_proctask(false);
  ChangeDnsConfig(CreateValidDnsConfig());

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4slow_4ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4slow_4timeout", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4slow_6timeout", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(responses[0]->complete());
  EXPECT_FALSE(responses[1]->complete());
  EXPECT_FALSE(responses[2]->complete());
  // The IPv6 of request 3 should have failed and resulted in cancelling the
  // IPv4 request.
  EXPECT_THAT(responses[3]->result_error(), IsError(ERR_DNS_TIMED_OUT));
  EXPECT_EQ(3u, num_running_dispatcher_jobs());

  dns_client_->CompleteDelayedTransactions();
  EXPECT_THAT(responses[0]->result_error(), IsOk());
  EXPECT_THAT(responses[0]->request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));

  EXPECT_THAT(responses[1]->result_error(), IsOk());
  EXPECT_THAT(responses[1]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));

  EXPECT_THAT(responses[2]->result_error(), IsError(ERR_DNS_TIMED_OUT));
}

// Test cases where transactions return secure or mixed secure/insecure results.
TEST_F(HostResolverManagerDnsTest, SecureOrMixedSecurityResults) {
  MockDnsClientRuleList rules;
  AddSecureDnsRule(&rules, "secure", dns_protocol::kTypeA,
                   MockDnsClientRule::OK, false /* delay */);
  AddSecureDnsRule(&rules, "secure", dns_protocol::kTypeAAAA,
                   MockDnsClientRule::OK, false /* delay */);
  AddDnsRule(&rules, "4insecure_6slowsecure", dns_protocol::kTypeA,
             MockDnsClientRule::OK, false /* delay */);
  AddSecureDnsRule(&rules, "4insecure_6slowsecure", dns_protocol::kTypeAAAA,
                   MockDnsClientRule::OK, true /* delay */);
  AddDnsRule(&rules, "4insecure_6slowemptysecure", dns_protocol::kTypeA,
             MockDnsClientRule::OK, false /* delay */);
  AddSecureDnsRule(&rules, "4insecure_6slowemptysecure",
                   dns_protocol::kTypeAAAA, MockDnsClientRule::EMPTY,
                   true /* delay */);
  AddDnsRule(&rules, "4insecureempty_6slowsecure", dns_protocol::kTypeA,
             MockDnsClientRule::EMPTY, false /* delay */);
  AddSecureDnsRule(&rules, "4insecureempty_6slowsecure",
                   dns_protocol::kTypeAAAA, MockDnsClientRule::OK,
                   true /* delay */);
  AddDnsRule(&rules, "4insecure_6slowfailsecure", dns_protocol::kTypeA,
             MockDnsClientRule::OK, false /* delay */);
  AddSecureDnsRule(&rules, "4insecure_6slowfailsecure", dns_protocol::kTypeAAAA,
                   MockDnsClientRule::FAIL, true /* delay */);
  AddSecureDnsRule(&rules, "4secure_6slowinsecure", dns_protocol::kTypeA,
                   MockDnsClientRule::OK, false /* delay */);
  AddDnsRule(&rules, "4secure_6slowinsecure", dns_protocol::kTypeAAAA,
             MockDnsClientRule::OK, true /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));
  set_allow_fallback_to_proctask(false);

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("secure", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4insecure_6slowsecure", 80), NetLogWithSource(),
          base::nullopt, request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4insecure_6slowemptysecure", 80), NetLogWithSource(),
          base::nullopt, request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4insecureempty_6slowsecure", 80), NetLogWithSource(),
          base::nullopt, request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4insecure_6slowfailsecure", 80), NetLogWithSource(),
          base::nullopt, request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("4secure_6slowinsecure", 80), NetLogWithSource(),
          base::nullopt, request_context_.get(), host_cache_.get())));

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(responses[0]->complete());
  EXPECT_FALSE(responses[1]->complete());
  EXPECT_FALSE(responses[2]->complete());
  EXPECT_FALSE(responses[3]->complete());
  EXPECT_FALSE(responses[4]->complete());
  EXPECT_FALSE(responses[5]->complete());

  dns_client_->CompleteDelayedTransactions();
  const std::pair<const HostCache::Key, HostCache::Entry>* cache_result;

  EXPECT_THAT(responses[0]->result_error(), IsOk());
  EXPECT_THAT(responses[0]->request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
  HostCache::Key key =
      HostCache::Key("secure", DnsQueryType::UNSPECIFIED,
                     0 /* host_resolver_flags */, HostResolverSource::ANY);
  key.secure = true;
  cache_result = GetCacheHit(key);
  EXPECT_TRUE(!!cache_result);

  EXPECT_TRUE(responses[1]->complete());
  EXPECT_THAT(responses[1]->result_error(), IsOk());
  EXPECT_THAT(responses[1]->request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
  cache_result = GetCacheHit(
      HostCache::Key("4insecure_6slowsecure", DnsQueryType::UNSPECIFIED,
                     0 /* host_resolver_flags */, HostResolverSource::ANY));
  EXPECT_TRUE(!!cache_result);

  EXPECT_TRUE(responses[2]->complete());
  EXPECT_THAT(responses[2]->result_error(), IsOk());
  EXPECT_THAT(responses[2]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("127.0.0.1", 80)));
  cache_result = GetCacheHit(
      HostCache::Key("4insecure_6slowemptysecure", DnsQueryType::UNSPECIFIED,
                     0 /* host_resolver_flags */, HostResolverSource::ANY));
  EXPECT_TRUE(!!cache_result);

  EXPECT_TRUE(responses[3]->complete());
  EXPECT_THAT(responses[3]->result_error(), IsOk());
  EXPECT_THAT(responses[3]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::1", 80)));
  cache_result = GetCacheHit(
      HostCache::Key("4insecureempty_6slowsecure", DnsQueryType::UNSPECIFIED,
                     0 /* host_resolver_flags */, HostResolverSource::ANY));
  EXPECT_TRUE(!!cache_result);

  EXPECT_TRUE(responses[4]->complete());
  EXPECT_THAT(responses[4]->result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(responses[4]->request()->GetAddressResults());
  cache_result = GetCacheHit(
      HostCache::Key("4insecure_6slowfailsecure", DnsQueryType::UNSPECIFIED,
                     0 /* host_resolver_flags */, HostResolverSource::ANY));
  EXPECT_TRUE(!!cache_result);

  EXPECT_TRUE(responses[5]->complete());
  EXPECT_THAT(responses[5]->result_error(), IsOk());
  EXPECT_THAT(responses[5]->request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
  cache_result = GetCacheHit(
      HostCache::Key("4secure_6slowinsecure", DnsQueryType::UNSPECIFIED,
                     0 /* host_resolver_flags */, HostResolverSource::ANY));
  EXPECT_TRUE(!!cache_result);
}

// Test the case where only a single transaction slot is available.
TEST_F(HostResolverManagerDnsTest, SerialResolver) {
  CreateSerialResolver();
  set_allow_fallback_to_proctask(false);
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_FALSE(response.complete());
  EXPECT_EQ(1u, num_running_dispatcher_jobs());

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(response.complete());
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
}

// Test the case where subsequent transactions are handled on transaction
// completion when only part of a multi-transaction request could be initially
// started.
TEST_F(HostResolverManagerDnsTest, AAAAStartsAfterOtherJobFinishes) {
  CreateResolverWithLimitsAndParams(3u, DefaultParams(proc_.get()),
                                    true /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);
  set_allow_fallback_to_proctask(false);
  ChangeDnsConfig(CreateValidDnsConfig());

  ResolveHostResponseHelper response0(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(2u, num_running_dispatcher_jobs());
  ResolveHostResponseHelper response1(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_EQ(3u, num_running_dispatcher_jobs());

  // Request 0's transactions should complete, starting Request 1's second
  // transaction, which should also complete.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, num_running_dispatcher_jobs());
  EXPECT_TRUE(response0.complete());
  EXPECT_FALSE(response1.complete());

  dns_client_->CompleteDelayedTransactions();
  EXPECT_THAT(response1.result_error(), IsOk());
  EXPECT_THAT(response1.request()->GetAddressResults().value().endpoints(),
              testing::UnorderedElementsAre(CreateExpected("127.0.0.1", 80),
                                            CreateExpected("::1", 80)));
}

// Tests the case that a Job with a single transaction receives an empty address
// list, triggering fallback to ProcTask.
TEST_F(HostResolverManagerDnsTest, IPv4EmptyFallback) {
  // Disable ipv6 to ensure we'll only try a single transaction for the host.
  CreateResolverWithLimitsAndParams(kMaxJobs, DefaultParams(proc_.get()),
                                    false /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);
  DnsConfig config = CreateValidDnsConfig();
  config.use_local_ipv6 = false;
  ChangeDnsConfig(config);

  proc_->AddRuleForAllFamilies("empty_fallback", "192.168.0.1",
                               HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6);
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("empty_fallback", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.0.1", 80)));
}

// Tests the case that a Job with two transactions receives two empty address
// lists, triggering fallback to ProcTask.
TEST_F(HostResolverManagerDnsTest, UnspecEmptyFallback) {
  ChangeDnsConfig(CreateValidDnsConfig());
  proc_->AddRuleForAllFamilies("empty_fallback", "192.168.0.1");
  proc_->SignalMultiple(1u);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("empty_fallback", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.0.1", 80)));
}

// Tests getting a new invalid DnsConfig while there are active DnsTasks.
TEST_F(HostResolverManagerDnsTest, InvalidDnsConfigWithPendingRequests) {
  // At most 3 jobs active at once.  This number is important, since we want
  // to make sure that aborting the first HostResolverManager::Job does not
  // trigger another DnsTransaction on the second Job when it releases its
  // second prioritized dispatcher slot.
  CreateResolverWithLimitsAndParams(3u, DefaultParams(proc_.get()),
                                    true /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);

  ChangeDnsConfig(CreateValidDnsConfig());

  proc_->AddRuleForAllFamilies("slow_nx1", "192.168.0.1");
  proc_->AddRuleForAllFamilies("slow_nx2", "192.168.0.2");
  proc_->AddRuleForAllFamilies("ok", "192.168.0.3");

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  // First active job gets two slots.
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("slow_nx1", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  // Next job gets one slot, and waits on another.
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("slow_nx2", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));

  EXPECT_EQ(3u, num_running_dispatcher_jobs());
  for (auto& response : responses) {
    EXPECT_FALSE(response->complete());
  }

  // Clear DNS config.  Request:
  // 0 fully in-progress should be aborted.
  // 1 partially in-progress should be fully aborted.
  // 2 queued up should run using ProcTask.
  ChangeDnsConfig(DnsConfig());
  EXPECT_THAT(responses[0]->result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_THAT(responses[1]->result_error(), IsError(ERR_NETWORK_CHANGED));
  EXPECT_FALSE(responses[2]->complete());

  // Finish up the third job.  Should bypass the DnsClient, and get its
  // results from MockHostResolverProc.
  proc_->SignalMultiple(1u);
  EXPECT_THAT(responses[2]->result_error(), IsOk());
  EXPECT_THAT(responses[2]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.0.3", 80)));
}

// Test that initial DNS config read signals do not abort pending requests
// when using DnsClient.
TEST_F(HostResolverManagerDnsTest, DontAbortOnInitialDNSConfigRead) {
  // DnsClient is enabled, but there's no DnsConfig, so the request should start
  // using ProcTask.
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host1", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_FALSE(response.complete());

  EXPECT_TRUE(proc_->WaitFor(1u));
  // Send the initial config read signal, with a valid config.
  SetInitialDnsConfig(CreateValidDnsConfig());
  proc_->SignalAll();

  EXPECT_THAT(response.result_error(), IsOk());
}

// Tests the case that DnsClient is automatically disabled due to failures
// while there are active DnsTasks.
TEST_F(HostResolverManagerDnsTest,
       AutomaticallyDisableDnsClientWithPendingRequests) {
  // Trying different limits is important for this test:  Different limits
  // result in different behavior when aborting in-progress DnsTasks.  Having
  // a DnsTask that has one job active and one in the queue when another job
  // occupying two slots has its DnsTask aborted is the case most likely to run
  // into problems.  Try limits between [1, 2 * # of non failure requests].
  for (size_t limit = 1u; limit < 10u; ++limit) {
    CreateResolverWithLimitsAndParams(limit, DefaultParams(proc_.get()),
                                      true /* ipv6_reachable */,
                                      true /* check_ipv6_on_wifi */);

    ChangeDnsConfig(CreateValidDnsConfig());

    // Queue up enough failures to disable DnsTasks.  These will all fall back
    // to ProcTasks, and succeed there.
    std::vector<std::unique_ptr<ResolveHostResponseHelper>> failure_responses;
    for (unsigned i = 0u; i < maximum_dns_failures(); ++i) {
      std::string host = base::StringPrintf("nx%u", i);
      proc_->AddRuleForAllFamilies(host, "192.168.0.1");
      failure_responses.emplace_back(
          std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
              HostPortPair(host, 80), NetLogWithSource(), base::nullopt,
              request_context_.get(), host_cache_.get())));
      EXPECT_FALSE(failure_responses[i]->complete());
    }

    // These requests should all bypass DnsTasks, due to the above failures,
    // so should end up using ProcTasks.
    proc_->AddRuleForAllFamilies("slow_ok1", "192.168.0.2");
    ResolveHostResponseHelper response0(resolver_->CreateRequest(
        HostPortPair("slow_ok1", 80), NetLogWithSource(), base::nullopt,
        request_context_.get(), host_cache_.get()));
    EXPECT_FALSE(response0.complete());
    proc_->AddRuleForAllFamilies("slow_ok2", "192.168.0.3");
    ResolveHostResponseHelper response1(resolver_->CreateRequest(
        HostPortPair("slow_ok2", 80), NetLogWithSource(), base::nullopt,
        request_context_.get(), host_cache_.get()));
    EXPECT_FALSE(response1.complete());
    proc_->AddRuleForAllFamilies("slow_ok3", "192.168.0.4");
    ResolveHostResponseHelper response2(resolver_->CreateRequest(
        HostPortPair("slow_ok3", 80), NetLogWithSource(), base::nullopt,
        request_context_.get(), host_cache_.get()));
    EXPECT_FALSE(response2.complete());

    // Requests specifying DNS source cannot fallback to ProcTask, so they
    // should be unaffected.
    HostResolver::ResolveHostParameters parameters;
    parameters.source = HostResolverSource::DNS;
    ResolveHostResponseHelper response_dns(resolver_->CreateRequest(
        HostPortPair("4slow_ok", 80), NetLogWithSource(), parameters,
        request_context_.get(), host_cache_.get()));
    EXPECT_FALSE(response_dns.complete());

    // Requests specifying SYSTEM source should be unaffected by disabling
    // DnsClient.
    proc_->AddRuleForAllFamilies("nx_ok", "192.168.0.5");
    parameters.source = HostResolverSource::SYSTEM;
    ResolveHostResponseHelper response_system(resolver_->CreateRequest(
        HostPortPair("nx_ok", 80), NetLogWithSource(), parameters,
        request_context_.get(), host_cache_.get()));
    EXPECT_FALSE(response_system.complete());

    proc_->SignalMultiple(maximum_dns_failures() + 5);

    for (size_t i = 0u; i < maximum_dns_failures(); ++i) {
      EXPECT_THAT(failure_responses[i]->result_error(), IsOk());
      EXPECT_THAT(failure_responses[i]
                      ->request()
                      ->GetAddressResults()
                      .value()
                      .endpoints(),
                  testing::ElementsAre(CreateExpected("192.168.0.1", 80)));
    }

    EXPECT_THAT(response0.result_error(), IsOk());
    EXPECT_THAT(response0.request()->GetAddressResults().value().endpoints(),
                testing::ElementsAre(CreateExpected("192.168.0.2", 80)));
    EXPECT_THAT(response1.result_error(), IsOk());
    EXPECT_THAT(response1.request()->GetAddressResults().value().endpoints(),
                testing::ElementsAre(CreateExpected("192.168.0.3", 80)));
    EXPECT_THAT(response2.result_error(), IsOk());
    EXPECT_THAT(response2.request()->GetAddressResults().value().endpoints(),
                testing::ElementsAre(CreateExpected("192.168.0.4", 80)));

    dns_client_->CompleteDelayedTransactions();
    EXPECT_THAT(response_dns.result_error(), IsOk());

    EXPECT_THAT(response_system.result_error(), IsOk());
    EXPECT_THAT(
        response_system.request()->GetAddressResults().value().endpoints(),
        testing::ElementsAre(CreateExpected("192.168.0.5", 80)));
  }
}

// Tests a call to SetDnsClient while there are active DnsTasks.
TEST_F(HostResolverManagerDnsTest,
       ManuallyDisableDnsClientWithPendingRequests) {
  // At most 3 jobs active at once.  This number is important, since we want to
  // make sure that aborting the first HostResolverManager::Job does not trigger
  // another DnsTransaction on the second Job when it releases its second
  // prioritized dispatcher slot.
  CreateResolverWithLimitsAndParams(3u, DefaultParams(proc_.get()),
                                    true /* ipv6_reachable */,
                                    true /* check_ipv6_on_wifi */);

  ChangeDnsConfig(CreateValidDnsConfig());

  proc_->AddRuleForAllFamilies("slow_ok1", "192.168.0.1");
  proc_->AddRuleForAllFamilies("slow_ok2", "192.168.0.2");
  proc_->AddRuleForAllFamilies("ok", "192.168.0.3");

  std::vector<std::unique_ptr<ResolveHostResponseHelper>> responses;
  // First active job gets two slots.
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("slow_ok1", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  EXPECT_FALSE(responses[0]->complete());
  // Next job gets one slot, and waits on another.
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("slow_ok2", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  EXPECT_FALSE(responses[1]->complete());
  // Next one is queued.
  responses.emplace_back(
      std::make_unique<ResolveHostResponseHelper>(resolver_->CreateRequest(
          HostPortPair("ok", 80), NetLogWithSource(), base::nullopt,
          request_context_.get(), host_cache_.get())));
  EXPECT_FALSE(responses[2]->complete());

  EXPECT_EQ(3u, num_running_dispatcher_jobs());

  // Clear DnsClient.  The two in-progress jobs should fall back to a ProcTask,
  // and the next one should be started with a ProcTask.
  resolver_->SetDnsClientEnabled(false);

  // All three in-progress requests should now be running a ProcTask.
  EXPECT_EQ(3u, num_running_dispatcher_jobs());
  proc_->SignalMultiple(3u);

  for (auto& response : responses) {
    EXPECT_THAT(response->result_error(), IsOk());
  }
  EXPECT_THAT(responses[0]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.0.1", 80)));
  EXPECT_THAT(responses[1]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.0.2", 80)));
  EXPECT_THAT(responses[2]->request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("192.168.0.3", 80)));
}

// When explicitly requesting source=DNS, no fallback allowed, so doing so with
// DnsClient disabled should result in an error.
TEST_F(HostResolverManagerDnsTest, DnsCallsWithDisabledDnsClient) {
  ChangeDnsConfig(CreateValidDnsConfig());
  resolver_->SetDnsClientEnabled(false);

  HostResolver::ResolveHostParameters params;
  params.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 80), NetLogWithSource(), params,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_FAILED));
}

TEST_F(HostResolverManagerDnsTest,
       DnsCallsWithDisabledDnsClient_DisabledAtConstruction) {
  HostResolver::ManagerOptions options = DefaultOptions();
  options.dns_client_enabled = false;
  CreateResolverWithOptionsAndParams(std::move(options),
                                     DefaultParams(proc_.get()),
                                     true /* ipv6_reachable */);
  ChangeDnsConfig(CreateValidDnsConfig());

  HostResolver::ResolveHostParameters params;
  params.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 80), NetLogWithSource(), params,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_FAILED));
}

// Same as DnsClient disabled, requests with source=DNS and no usable DnsConfig
// should result in an error.
TEST_F(HostResolverManagerDnsTest, DnsCallsWithNoDnsConfig) {
  ChangeDnsConfig(DnsConfig());

  HostResolver::ResolveHostParameters params;
  params.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 80), NetLogWithSource(), params,
      request_context_.get(), host_cache_.get()));

  EXPECT_THAT(response.result_error(), IsError(ERR_FAILED));
}

TEST_F(HostResolverManagerDnsTest, NoCheckIpv6OnWifi) {
  // CreateSerialResolver will destroy the current resolver_ which will attempt
  // to remove itself from the NetworkChangeNotifier. If this happens after a
  // new NetworkChangeNotifier is active, then it will not remove itself from
  // the old NetworkChangeNotifier which is a potential use-after-free.
  DestroyResolver();
  test::ScopedMockNetworkChangeNotifier notifier;
  // Serial resolver to guarantee order of resolutions.
  CreateSerialResolver(false /* check_ipv6_on_wifi */);

  notifier.mock_network_change_notifier()->SetConnectionType(
      NetworkChangeNotifier::CONNECTION_WIFI);
  // Needed so IPv6 availability check isn't skipped.
  ChangeDnsConfig(CreateValidDnsConfig());

  proc_->AddRule("h1", ADDRESS_FAMILY_UNSPECIFIED, "::3");
  proc_->AddRule("h1", ADDRESS_FAMILY_IPV4, "1.0.0.1");
  proc_->AddRule("h1", ADDRESS_FAMILY_IPV4, "1.0.0.1",
                 HOST_RESOLVER_DEFAULT_FAMILY_SET_DUE_TO_NO_IPV6);
  proc_->AddRule("h1", ADDRESS_FAMILY_IPV6, "::2");

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("h1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::A;
  ResolveHostResponseHelper v4_response(resolver_->CreateRequest(
      HostPortPair("h1", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  parameters.dns_query_type = DnsQueryType::AAAA;
  ResolveHostResponseHelper v6_response(resolver_->CreateRequest(
      HostPortPair("h1", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  proc_->SignalMultiple(3u);

  // Should revert to only IPV4 request.
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_THAT(response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("1.0.0.1", 80)));

  EXPECT_THAT(v4_response.result_error(), IsOk());
  EXPECT_THAT(v4_response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("1.0.0.1", 80)));
  EXPECT_THAT(v6_response.result_error(), IsOk());
  EXPECT_THAT(v6_response.request()->GetAddressResults().value().endpoints(),
              testing::ElementsAre(CreateExpected("::2", 80)));

  // Now repeat the test on non-wifi to check that IPv6 is used as normal
  // after the network changes.
  notifier.mock_network_change_notifier()->SetConnectionType(
      NetworkChangeNotifier::CONNECTION_4G);
  base::RunLoop().RunUntilIdle();  // Wait for NetworkChangeNotifier.

  ResolveHostResponseHelper no_wifi_response(resolver_->CreateRequest(
      HostPortPair("h1", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  parameters.dns_query_type = DnsQueryType::A;
  ResolveHostResponseHelper no_wifi_v4_response(resolver_->CreateRequest(
      HostPortPair("h1", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  parameters.dns_query_type = DnsQueryType::AAAA;
  ResolveHostResponseHelper no_wifi_v6_response(resolver_->CreateRequest(
      HostPortPair("h1", 80), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));

  proc_->SignalMultiple(3u);

  // IPV6 should be available.
  EXPECT_THAT(no_wifi_response.result_error(), IsOk());
  EXPECT_THAT(
      no_wifi_response.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("::3", 80)));

  EXPECT_THAT(no_wifi_v4_response.result_error(), IsOk());
  EXPECT_THAT(
      no_wifi_v4_response.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("1.0.0.1", 80)));
  EXPECT_THAT(no_wifi_v6_response.result_error(), IsOk());
  EXPECT_THAT(
      no_wifi_v6_response.request()->GetAddressResults().value().endpoints(),
      testing::ElementsAre(CreateExpected("::2", 80)));
}

TEST_F(HostResolverManagerDnsTest, NotFoundTTL) {
  CreateResolver();
  set_allow_fallback_to_proctask(false);
  ChangeDnsConfig(CreateValidDnsConfig());

  // NODATA
  ResolveHostResponseHelper no_data_response(resolver_->CreateRequest(
      HostPortPair("empty", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(no_data_response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(no_data_response.request()->GetAddressResults());
  HostCache::Key key("empty", DnsQueryType::UNSPECIFIED, 0,
                     HostResolverSource::ANY);
  HostCache::EntryStaleness staleness;
  const std::pair<const HostCache::Key, HostCache::Entry>* cache_result =
      host_cache_->Lookup(key, base::TimeTicks::Now(),
                          false /* ignore_secure */);
  EXPECT_TRUE(!!cache_result);
  EXPECT_TRUE(cache_result->second.has_ttl());
  EXPECT_THAT(cache_result->second.ttl(), base::TimeDelta::FromSeconds(86400));

  // NXDOMAIN
  ResolveHostResponseHelper no_domain_response(resolver_->CreateRequest(
      HostPortPair("nodomain", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(no_domain_response.result_error(),
              IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(no_domain_response.request()->GetAddressResults());
  HostCache::Key nxkey("nodomain", DnsQueryType::UNSPECIFIED, 0,
                       HostResolverSource::ANY);
  cache_result = host_cache_->Lookup(nxkey, base::TimeTicks::Now(),
                                     false /* ignore_secure */);
  EXPECT_TRUE(!!cache_result);
  EXPECT_TRUE(cache_result->second.has_ttl());
  EXPECT_THAT(cache_result->second.ttl(), base::TimeDelta::FromSeconds(86400));
}

TEST_F(HostResolverManagerDnsTest, CachedError) {
  CreateResolver();
  set_allow_fallback_to_proctask(false);
  ChangeDnsConfig(CreateValidDnsConfig());

  HostResolver::ResolveHostParameters cache_only_parameters;
  cache_only_parameters.source = HostResolverSource::LOCAL_ONLY;

  // Expect cache initially empty.
  ResolveHostResponseHelper cache_miss_response(resolver_->CreateRequest(
      HostPortPair("nodomain", 80), NetLogWithSource(), cache_only_parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(cache_miss_response.result_error(), IsError(ERR_DNS_CACHE_MISS));
  EXPECT_FALSE(cache_miss_response.request()->GetStaleInfo());

  // Populate cache with an error.
  ResolveHostResponseHelper no_domain_response(resolver_->CreateRequest(
      HostPortPair("nodomain", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(no_domain_response.result_error(),
              IsError(ERR_NAME_NOT_RESOLVED));

  // Expect the error result can be resolved from the cache.
  ResolveHostResponseHelper cache_hit_response(resolver_->CreateRequest(
      HostPortPair("nodomain", 80), NetLogWithSource(), cache_only_parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(cache_hit_response.result_error(),
              IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(cache_hit_response.request()->GetStaleInfo().value().is_stale());
}

TEST_F(HostResolverManagerDnsTest, NoCanonicalName) {
  MockDnsClientRuleList rules;
  AddDnsRule(&rules, "alias", dns_protocol::kTypeA, IPAddress::IPv4Localhost(),
             "canonical", false /* delay */);
  AddDnsRule(&rules, "alias", dns_protocol::kTypeAAAA,
             IPAddress::IPv6Localhost(), "canonical", false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));
  set_allow_fallback_to_proctask(false);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("alias", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_THAT(response.result_error(), IsOk());

  // HostResolver may still give name, but if so, it must be correct.
  std::string result_name =
      response.request()->GetAddressResults().value().canonical_name();
  EXPECT_TRUE(result_name.empty() || result_name == "canonical");
}

TEST_F(HostResolverManagerDnsTest, CanonicalName) {
  MockDnsClientRuleList rules;
  AddDnsRule(&rules, "alias", dns_protocol::kTypeA, IPAddress::IPv4Localhost(),
             "canonical", false /* delay */);
  AddDnsRule(&rules, "alias", dns_protocol::kTypeAAAA,
             IPAddress::IPv6Localhost(), "canonical", false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));
  set_allow_fallback_to_proctask(false);

  HostResolver::ResolveHostParameters params;
  params.include_canonical_name = true;
  params.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("alias", 80), NetLogWithSource(), params,
      request_context_.get(), host_cache_.get()));
  ASSERT_THAT(response.result_error(), IsOk());

  EXPECT_EQ(response.request()->GetAddressResults().value().canonical_name(),
            "canonical");
}

TEST_F(HostResolverManagerDnsTest, CanonicalName_PreferV6) {
  MockDnsClientRuleList rules;
  AddDnsRule(&rules, "alias", dns_protocol::kTypeA, IPAddress::IPv4Localhost(),
             "wrong", false /* delay */);
  AddDnsRule(&rules, "alias", dns_protocol::kTypeAAAA,
             IPAddress::IPv6Localhost(), "correct", true /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));
  set_allow_fallback_to_proctask(false);

  HostResolver::ResolveHostParameters params;
  params.include_canonical_name = true;
  params.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("alias", 80), NetLogWithSource(), params,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());
  base::RunLoop().RunUntilIdle();
  dns_client_->CompleteDelayedTransactions();
  ASSERT_THAT(response.result_error(), IsOk());
  EXPECT_EQ(response.request()->GetAddressResults().value().canonical_name(),
            "correct");
}

TEST_F(HostResolverManagerDnsTest, CanonicalName_V4Only) {
  MockDnsClientRuleList rules;
  AddDnsRule(&rules, "alias", dns_protocol::kTypeA, IPAddress::IPv4Localhost(),
             "correct", false /* delay */);
  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));
  set_allow_fallback_to_proctask(false);

  HostResolver::ResolveHostParameters params;
  params.dns_query_type = DnsQueryType::A;
  params.include_canonical_name = true;
  params.source = HostResolverSource::DNS;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("alias", 80), NetLogWithSource(), params,
      request_context_.get(), host_cache_.get()));
  ASSERT_THAT(response.result_error(), IsOk());
  EXPECT_EQ(response.request()->GetAddressResults().value().canonical_name(),
            "correct");
}

// Test that without specifying source, a request that would otherwise be
// handled by DNS is sent to the system resolver if cannonname is requested.
TEST_F(HostResolverManagerDnsTest, CanonicalNameForcesProc) {
  // Disable fallback to ensure system resolver is used directly, not via
  // fallback.
  set_allow_fallback_to_proctask(false);

  proc_->AddRuleForAllFamilies("nx_succeed", "192.168.1.102",
                               HOST_RESOLVER_CANONNAME, "canonical");
  proc_->SignalMultiple(1u);

  ChangeDnsConfig(CreateValidDnsConfig());

  HostResolver::ResolveHostParameters params;
  params.include_canonical_name = true;
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("nx_succeed", 80), NetLogWithSource(), params,
      request_context_.get(), host_cache_.get()));
  ASSERT_THAT(response.result_error(), IsOk());

  EXPECT_EQ(response.request()->GetAddressResults().value().canonical_name(),
            "canonical");
}

TEST_F(HostResolverManagerTest, ResolveLocalHostname) {
  AddressList addresses;

  TestBothLoopbackIPs("localhost");
  TestBothLoopbackIPs("localhoST");
  TestBothLoopbackIPs("localhost.");
  TestBothLoopbackIPs("localhoST.");
  TestBothLoopbackIPs("localhost.localdomain");
  TestBothLoopbackIPs("localhost.localdomAIn");
  TestBothLoopbackIPs("localhost.localdomain.");
  TestBothLoopbackIPs("localhost.localdomAIn.");
  TestBothLoopbackIPs("foo.localhost");
  TestBothLoopbackIPs("foo.localhOSt");
  TestBothLoopbackIPs("foo.localhost.");
  TestBothLoopbackIPs("foo.localhOSt.");

  TestIPv6LoopbackOnly("localhost6");
  TestIPv6LoopbackOnly("localhoST6");
  TestIPv6LoopbackOnly("localhost6.");
  TestIPv6LoopbackOnly("localhost6.localdomain6");
  TestIPv6LoopbackOnly("localhost6.localdomain6.");

  EXPECT_FALSE(ResolveLocalHostname("127.0.0.1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("::1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("0:0:0:0:0:0:0:1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("localhostx", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("localhost.x", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("foo.localdomain", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("foo.localdomain.x", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("localhost6x", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("localhost.localdomain6", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("localhost6.localdomain", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("127.0.0.1.1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname(".127.0.0.255", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("::2", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("::1:1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("0:0:0:0:1:0:0:1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("::1:1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("0:0:0:0:0:0:0:0:1", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("foo.localhost.com", &addresses));
  EXPECT_FALSE(ResolveLocalHostname("foo.localhoste", &addresses));
}

TEST_F(HostResolverManagerDnsTest, ResolveDnsOverHttpsServerName) {
  MockDnsClientRuleList rules;
  rules.emplace_back(
      "dns.example2.com", dns_protocol::kTypeA, DnsConfig::SecureDnsMode::OFF,
      MockDnsClientRule::Result(MockDnsClientRule::OK), false /* delay */);
  rules.emplace_back("dns.example2.com", dns_protocol::kTypeAAAA,
                     DnsConfig::SecureDnsMode::OFF,
                     MockDnsClientRule::Result(MockDnsClientRule::OK),
                     false /* delay */);
  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  DnsConfigOverrides overrides;
  overrides.dns_over_https_servers.emplace({DnsConfig::DnsOverHttpsServerConfig(
      "https://dns.example.com/", true /*  use_post */)});
  overrides.dns_over_https_servers.emplace({DnsConfig::DnsOverHttpsServerConfig(
      "https://dns.example2.com/dns-query{?dns}", false /* use_post */)});
  resolver_->SetDnsConfigOverrides(overrides);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("dns.example2.com", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_THAT(response.result_error(), IsOk());
}

TEST_F(HostResolverManagerDnsTest, AddDnsOverHttpsServerAfterConfig) {
  DestroyResolver();
  test::ScopedMockNetworkChangeNotifier notifier;
  CreateSerialResolver();  // To guarantee order of resolutions.
  notifier.mock_network_change_notifier()->SetConnectionType(
      NetworkChangeNotifier::CONNECTION_WIFI);
  ChangeDnsConfig(CreateValidDnsConfig());

  resolver_->SetDnsClientEnabled(true);
  std::string server("https://dnsserver.example.net/dns-query{?dns}");
  DnsConfigOverrides overrides;
  overrides.dns_over_https_servers.emplace(
      {DnsConfig::DnsOverHttpsServerConfig(server, true)});
  resolver_->SetDnsConfigOverrides(overrides);
  base::DictionaryValue* config;

  auto value = resolver_->GetDnsConfigAsValue();
  EXPECT_TRUE(value);
  if (!value)
    return;
  value->GetAsDictionary(&config);
  base::ListValue* doh_servers;
  config->GetListWithoutPathExpansion("doh_servers", &doh_servers);
  EXPECT_TRUE(doh_servers);
  if (!doh_servers)
    return;
  EXPECT_EQ(doh_servers->GetSize(), 1u);
  base::DictionaryValue* server_method;
  EXPECT_TRUE(doh_servers->GetDictionary(0, &server_method));
  bool use_post;
  EXPECT_TRUE(server_method->GetBoolean("use_post", &use_post));
  EXPECT_TRUE(use_post);
  std::string server_template;
  EXPECT_TRUE(server_method->GetString("server_template", &server_template));
  EXPECT_EQ(server_template, server);
}

TEST_F(HostResolverManagerDnsTest, AddDnsOverHttpsServerBeforeConfig) {
  DestroyResolver();
  test::ScopedMockNetworkChangeNotifier notifier;
  CreateSerialResolver();  // To guarantee order of resolutions.
  resolver_->SetDnsClientEnabled(true);
  std::string server("https://dnsserver.example.net/dns-query{?dns}");
  DnsConfigOverrides overrides;
  overrides.dns_over_https_servers.emplace(
      {DnsConfig::DnsOverHttpsServerConfig(server, true)});
  resolver_->SetDnsConfigOverrides(overrides);

  notifier.mock_network_change_notifier()->SetConnectionType(
      NetworkChangeNotifier::CONNECTION_WIFI);
  ChangeDnsConfig(CreateValidDnsConfig());

  base::DictionaryValue* config;
  auto value = resolver_->GetDnsConfigAsValue();
  EXPECT_TRUE(value);
  if (!value)
    return;
  value->GetAsDictionary(&config);
  base::ListValue* doh_servers;
  config->GetListWithoutPathExpansion("doh_servers", &doh_servers);
  EXPECT_TRUE(doh_servers);
  if (!doh_servers)
    return;
  EXPECT_EQ(doh_servers->GetSize(), 1u);
  base::DictionaryValue* server_method;
  EXPECT_TRUE(doh_servers->GetDictionary(0, &server_method));
  bool use_post;
  EXPECT_TRUE(server_method->GetBoolean("use_post", &use_post));
  EXPECT_TRUE(use_post);
  std::string server_template;
  EXPECT_TRUE(server_method->GetString("server_template", &server_template));
  EXPECT_EQ(server_template, server);
}

TEST_F(HostResolverManagerDnsTest, AddDnsOverHttpsServerBeforeClient) {
  DestroyResolver();
  test::ScopedMockNetworkChangeNotifier notifier;
  CreateSerialResolver();  // To guarantee order of resolutions.
  std::string server("https://dnsserver.example.net/dns-query{?dns}");
  DnsConfigOverrides overrides;
  overrides.dns_over_https_servers.emplace(
      {DnsConfig::DnsOverHttpsServerConfig(server, true)});
  resolver_->SetDnsConfigOverrides(overrides);

  notifier.mock_network_change_notifier()->SetConnectionType(
      NetworkChangeNotifier::CONNECTION_WIFI);
  ChangeDnsConfig(CreateValidDnsConfig());

  resolver_->SetDnsClientEnabled(true);

  base::DictionaryValue* config;
  auto value = resolver_->GetDnsConfigAsValue();
  EXPECT_TRUE(value);
  if (!value)
    return;
  value->GetAsDictionary(&config);
  base::ListValue* doh_servers;
  config->GetListWithoutPathExpansion("doh_servers", &doh_servers);
  EXPECT_TRUE(doh_servers);
  if (!doh_servers)
    return;
  EXPECT_EQ(doh_servers->GetSize(), 1u);
  base::DictionaryValue* server_method;
  EXPECT_TRUE(doh_servers->GetDictionary(0, &server_method));
  bool use_post;
  EXPECT_TRUE(server_method->GetBoolean("use_post", &use_post));
  EXPECT_TRUE(use_post);
  std::string server_template;
  EXPECT_TRUE(server_method->GetString("server_template", &server_template));
  EXPECT_EQ(server_template, server);
}

TEST_F(HostResolverManagerDnsTest, AddDnsOverHttpsServerAndThenRemove) {
  DestroyResolver();
  test::ScopedMockNetworkChangeNotifier notifier;
  CreateSerialResolver();  // To guarantee order of resolutions.
  std::string server("https://dns.example.com/");
  DnsConfigOverrides overrides;
  overrides.dns_over_https_servers.emplace(
      {DnsConfig::DnsOverHttpsServerConfig(server, true)});
  resolver_->SetDnsConfigOverrides(overrides);

  notifier.mock_network_change_notifier()->SetConnectionType(
      NetworkChangeNotifier::CONNECTION_WIFI);
  ChangeDnsConfig(CreateValidDnsConfig());

  resolver_->SetDnsClientEnabled(true);

  base::DictionaryValue* config;
  auto value = resolver_->GetDnsConfigAsValue();
  EXPECT_TRUE(value);
  if (!value)
    return;
  value->GetAsDictionary(&config);
  base::ListValue* doh_servers;
  config->GetListWithoutPathExpansion("doh_servers", &doh_servers);
  EXPECT_TRUE(doh_servers);
  if (!doh_servers)
    return;
  EXPECT_EQ(doh_servers->GetSize(), 1u);
  base::DictionaryValue* server_method;
  EXPECT_TRUE(doh_servers->GetDictionary(0, &server_method));
  bool use_post;
  EXPECT_TRUE(server_method->GetBoolean("use_post", &use_post));
  EXPECT_TRUE(use_post);
  std::string server_template;
  EXPECT_TRUE(server_method->GetString("server_template", &server_template));
  EXPECT_EQ(server_template, server);

  resolver_->SetDnsConfigOverrides(DnsConfigOverrides());
  value = resolver_->GetDnsConfigAsValue();
  EXPECT_TRUE(value);
  if (!value)
    return;
  value->GetAsDictionary(&config);
  config->GetListWithoutPathExpansion("doh_servers", &doh_servers);
  EXPECT_TRUE(doh_servers);
  if (!doh_servers)
    return;
  EXPECT_EQ(doh_servers->GetSize(), 0u);
}

TEST_F(HostResolverManagerDnsTest, SetDnsConfigOverrides) {
  DnsConfig original_config = CreateValidDnsConfig();
  ChangeDnsConfig(original_config);

  // Confirm pre-override state.
  ASSERT_TRUE(original_config.Equals(*dns_client_->GetConfig()));

  DnsConfigOverrides overrides;
  const std::vector<IPEndPoint> nameservers = {
      CreateExpected("192.168.0.1", 92)};
  overrides.nameservers = nameservers;
  const std::vector<std::string> search = {"str"};
  overrides.search = search;
  const DnsHosts hosts = {
      {DnsHostsKey("host", ADDRESS_FAMILY_IPV4), IPAddress(192, 168, 1, 1)}};
  overrides.hosts = hosts;
  overrides.append_to_multi_label_name = false;
  overrides.randomize_ports = true;
  const int ndots = 5;
  overrides.ndots = ndots;
  const base::TimeDelta timeout = base::TimeDelta::FromSeconds(10);
  overrides.timeout = timeout;
  const int attempts = 20;
  overrides.attempts = attempts;
  overrides.rotate = true;
  overrides.use_local_ipv6 = true;
  const std::vector<DnsConfig::DnsOverHttpsServerConfig>
      dns_over_https_servers = {
          DnsConfig::DnsOverHttpsServerConfig("dns.example.com", true)};
  overrides.dns_over_https_servers = dns_over_https_servers;
  const DnsConfig::SecureDnsMode secure_dns_mode =
      DnsConfig::SecureDnsMode::SECURE;
  overrides.secure_dns_mode = secure_dns_mode;

  // This test is expected to test overriding all fields.
  EXPECT_TRUE(overrides.OverridesEverything());

  resolver_->SetDnsConfigOverrides(overrides);

  const DnsConfig* overridden_config = dns_client_->GetConfig();
  EXPECT_EQ(nameservers, overridden_config->nameservers);
  EXPECT_EQ(search, overridden_config->search);
  EXPECT_EQ(hosts, overridden_config->hosts);
  EXPECT_FALSE(overridden_config->append_to_multi_label_name);
  EXPECT_TRUE(overridden_config->randomize_ports);
  EXPECT_EQ(ndots, overridden_config->ndots);
  EXPECT_EQ(timeout, overridden_config->timeout);
  EXPECT_EQ(attempts, overridden_config->attempts);
  EXPECT_TRUE(overridden_config->rotate);
  EXPECT_TRUE(overridden_config->use_local_ipv6);
  EXPECT_EQ(dns_over_https_servers, overridden_config->dns_over_https_servers);
  EXPECT_EQ(secure_dns_mode, overridden_config->secure_dns_mode);
}

TEST_F(HostResolverManagerDnsTest,
       SetDnsConfigOverrides_OverrideEverythingCreation) {
  DnsConfig original_config = CreateValidDnsConfig();
  ChangeDnsConfig(original_config);

  // Confirm pre-override state.
  ASSERT_TRUE(original_config.Equals(*dns_client_->GetConfig()));
  ASSERT_FALSE(original_config.Equals(DnsConfig()));

  DnsConfigOverrides overrides =
      DnsConfigOverrides::CreateOverridingEverythingWithDefaults();
  EXPECT_TRUE(overrides.OverridesEverything());

  // Ensure config is valid by setting a nameserver.
  std::vector<IPEndPoint> nameservers = {CreateExpected("1.2.3.4", 50)};
  overrides.nameservers = nameservers;
  EXPECT_TRUE(overrides.OverridesEverything());

  resolver_->SetDnsConfigOverrides(overrides);

  DnsConfig expected;
  expected.nameservers = nameservers;
  EXPECT_TRUE(dns_client_->GetConfig()->Equals(DnsConfig(expected)));
}

TEST_F(HostResolverManagerDnsTest, SetDnsConfigOverrides_PartialOverride) {
  DnsConfig original_config = CreateValidDnsConfig();
  ChangeDnsConfig(original_config);

  // Confirm pre-override state.
  ASSERT_TRUE(original_config.Equals(*dns_client_->GetConfig()));

  DnsConfigOverrides overrides;
  const std::vector<IPEndPoint> nameservers = {
      CreateExpected("192.168.0.2", 192)};
  overrides.nameservers = nameservers;
  overrides.rotate = true;
  EXPECT_FALSE(overrides.OverridesEverything());

  resolver_->SetDnsConfigOverrides(overrides);

  const DnsConfig* overridden_config = dns_client_->GetConfig();
  EXPECT_EQ(nameservers, overridden_config->nameservers);
  EXPECT_EQ(original_config.search, overridden_config->search);
  EXPECT_EQ(original_config.hosts, overridden_config->hosts);
  EXPECT_TRUE(overridden_config->append_to_multi_label_name);
  EXPECT_FALSE(overridden_config->randomize_ports);
  EXPECT_EQ(original_config.ndots, overridden_config->ndots);
  EXPECT_EQ(original_config.timeout, overridden_config->timeout);
  EXPECT_EQ(original_config.attempts, overridden_config->attempts);
  EXPECT_TRUE(overridden_config->rotate);
  EXPECT_FALSE(overridden_config->use_local_ipv6);
  EXPECT_EQ(original_config.dns_over_https_servers,
            overridden_config->dns_over_https_servers);
  EXPECT_EQ(original_config.secure_dns_mode,
            overridden_config->secure_dns_mode);
}

// Test that overridden configs are reapplied over a changed underlying system
// config.
TEST_F(HostResolverManagerDnsTest, SetDnsConfigOverrides_NewConfig) {
  DnsConfig original_config = CreateValidDnsConfig();
  ChangeDnsConfig(original_config);

  // Confirm pre-override state.
  ASSERT_TRUE(original_config.Equals(*dns_client_->GetConfig()));

  DnsConfigOverrides overrides;
  const std::vector<IPEndPoint> nameservers = {
      CreateExpected("192.168.0.2", 192)};
  overrides.nameservers = nameservers;

  resolver_->SetDnsConfigOverrides(overrides);
  ASSERT_EQ(nameservers, dns_client_->GetConfig()->nameservers);

  DnsConfig new_config = original_config;
  new_config.attempts = 103;
  ASSERT_NE(nameservers, new_config.nameservers);
  ChangeDnsConfig(new_config);

  const DnsConfig* overridden_config = dns_client_->GetConfig();
  EXPECT_EQ(nameservers, overridden_config->nameservers);
  EXPECT_EQ(new_config.attempts, overridden_config->attempts);
}

TEST_F(HostResolverManagerDnsTest, SetDnsConfigOverrides_ClearOverrides) {
  DnsConfig original_config = CreateValidDnsConfig();
  ChangeDnsConfig(original_config);

  DnsConfigOverrides overrides;
  overrides.attempts = 245;
  resolver_->SetDnsConfigOverrides(overrides);

  ASSERT_FALSE(original_config.Equals(*dns_client_->GetConfig()));

  resolver_->SetDnsConfigOverrides(DnsConfigOverrides());
  EXPECT_TRUE(original_config.Equals(*dns_client_->GetConfig()));
}

TEST_F(HostResolverManagerDnsTest, FlushCacheOnDnsConfigOverridesChange) {
  ChangeDnsConfig(CreateValidDnsConfig());

  HostResolver::ResolveHostParameters local_source_parameters;
  local_source_parameters.source = HostResolverSource::LOCAL_ONLY;

  // Populate cache.
  ResolveHostResponseHelper initial_response(resolver_->CreateRequest(
      HostPortPair("ok", 70), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(initial_response.result_error(), IsOk());

  // Confirm result now cached.
  ResolveHostResponseHelper cached_response(resolver_->CreateRequest(
      HostPortPair("ok", 75), NetLogWithSource(), local_source_parameters,
      request_context_.get(), host_cache_.get()));
  ASSERT_THAT(cached_response.result_error(), IsOk());
  ASSERT_TRUE(cached_response.request()->GetStaleInfo());

  // Flush cache by triggering a DnsConfigOverrides change.
  DnsConfigOverrides overrides;
  overrides.attempts = 4;
  resolver_->SetDnsConfigOverrides(overrides);

  // Expect no longer cached
  ResolveHostResponseHelper flushed_response(resolver_->CreateRequest(
      HostPortPair("ok", 80), NetLogWithSource(), local_source_parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(flushed_response.result_error(), IsError(ERR_DNS_CACHE_MISS));
}

// Test that even when using config overrides, a change to the base system
// config cancels pending requests.
TEST_F(HostResolverManagerDnsTest, CancellationOnBaseConfigChange) {
  DnsConfig original_config = CreateValidDnsConfig();
  ChangeDnsConfig(original_config);

  DnsConfigOverrides overrides;
  overrides.nameservers.emplace({CreateExpected("123.123.123.123", 80)});
  ASSERT_FALSE(overrides.OverridesEverything());
  resolver_->SetDnsConfigOverrides(overrides);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());

  DnsConfig new_config = original_config;
  new_config.attempts = 103;
  ChangeDnsConfig(new_config);

  EXPECT_THAT(response.result_error(), IsError(ERR_NETWORK_CHANGED));
}

// Test that when all configuration is overridden, system configuration changes
// do not cancel requests.
TEST_F(HostResolverManagerDnsTest,
       CancellationOnBaseConfigChange_OverridesEverything) {
  DnsConfig original_config = CreateValidDnsConfig();
  ChangeDnsConfig(original_config);

  DnsConfigOverrides overrides =
      DnsConfigOverrides::CreateOverridingEverythingWithDefaults();
  overrides.nameservers.emplace({CreateExpected("123.123.123.123", 80)});
  ASSERT_TRUE(overrides.OverridesEverything());
  resolver_->SetDnsConfigOverrides(overrides);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());

  DnsConfig new_config = original_config;
  new_config.attempts = 103;
  ChangeDnsConfig(new_config);

  dns_client_->CompleteDelayedTransactions();
  EXPECT_THAT(response.result_error(), IsOk());
}

// Test that in-progress queries are cancelled on applying new DNS config
// overrides, same as receiving a new DnsConfig from the system.
TEST_F(HostResolverManagerDnsTest, CancelQueriesOnSettingOverrides) {
  ChangeDnsConfig(CreateValidDnsConfig());
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());

  DnsConfigOverrides overrides;
  overrides.attempts = 123;
  resolver_->SetDnsConfigOverrides(overrides);

  EXPECT_THAT(response.result_error(), IsError(ERR_NETWORK_CHANGED));
}

// Queries should not be cancelled if equal overrides are set.
TEST_F(HostResolverManagerDnsTest,
       CancelQueriesOnSettingOverrides_SameOverrides) {
  ChangeDnsConfig(CreateValidDnsConfig());
  DnsConfigOverrides overrides;
  overrides.attempts = 123;
  resolver_->SetDnsConfigOverrides(overrides);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());

  resolver_->SetDnsConfigOverrides(overrides);

  dns_client_->CompleteDelayedTransactions();
  EXPECT_THAT(response.result_error(), IsOk());
}

// Test that in-progress queries are cancelled on clearing DNS config overrides,
// same as receiving a new DnsConfig from the system.
TEST_F(HostResolverManagerDnsTest, CancelQueriesOnClearingOverrides) {
  ChangeDnsConfig(CreateValidDnsConfig());
  DnsConfigOverrides overrides;
  overrides.attempts = 123;
  resolver_->SetDnsConfigOverrides(overrides);

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());

  resolver_->SetDnsConfigOverrides(DnsConfigOverrides());

  EXPECT_THAT(response.result_error(), IsError(ERR_NETWORK_CHANGED));
}

// Queries should not be cancelled on clearing overrides if there were not any
// overrides.
TEST_F(HostResolverManagerDnsTest,
       CancelQueriesOnClearingOverrides_NoOverrides) {
  ChangeDnsConfig(CreateValidDnsConfig());
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("4slow_ok", 80), NetLogWithSource(), base::nullopt,
      request_context_.get(), host_cache_.get()));
  ASSERT_FALSE(response.complete());

  resolver_->SetDnsConfigOverrides(DnsConfigOverrides());

  dns_client_->CompleteDelayedTransactions();
  EXPECT_THAT(response.result_error(), IsOk());
}

// Test HostResolverManager::UpdateModeForHistogram.
TEST_F(HostResolverManagerDnsTest, ModeForHistogram) {
  // Test Async resolver is detected.
  ChangeDnsConfig(CreateValidDnsConfig());
  EXPECT_EQ(resolver_->mode_for_histogram_,
            HostResolverManager::MODE_FOR_HISTOGRAM_ASYNC_DNS);

  // Test upgradability is detected for async DNS.
  static const std::vector<const char*> upgradable_servers(
      {// Google Public DNS
       "8.8.8.8", "8.8.4.4", "2001:4860:4860::8888", "2001:4860:4860::8844",
       // Cloudflare DNS
       "1.1.1.1", "1.0.0.1", "2606:4700:4700::1111", "2606:4700:4700::1001",
       // Quad9 DNS
       "9.9.9.9", "149.112.112.112", "2620:fe::fe", "2620:fe::9"});
  for (const char* upgradable_server : upgradable_servers) {
    IPAddress ip_address;
    ASSERT_TRUE(ip_address.AssignFromIPLiteral(upgradable_server));
    DnsConfig dns_config;
    dns_config.nameservers.push_back(
        IPEndPoint(ip_address, dns_protocol::kDefaultPort));
    ChangeDnsConfig(dns_config);
    EXPECT_EQ(
        resolver_->mode_for_histogram_,
        HostResolverManager::MODE_FOR_HISTOGRAM_ASYNC_DNS_PRIVATE_SUPPORTS_DOH);
  }

  // Test system resolver is detected.
  resolver_->SetDnsClientEnabled(false);
  ChangeDnsConfig(CreateValidDnsConfig());
  EXPECT_EQ(resolver_->mode_for_histogram_,
            HostResolverManager::MODE_FOR_HISTOGRAM_SYSTEM);

  // Test upgradability is detected for system resolver.
  for (const char* upgradable_server : upgradable_servers) {
    IPAddress ip_address;
    ASSERT_TRUE(ip_address.AssignFromIPLiteral(upgradable_server));
    DnsConfig dns_config;
    dns_config.nameservers.push_back(
        IPEndPoint(ip_address, dns_protocol::kDefaultPort));
    ChangeDnsConfig(dns_config);
    EXPECT_EQ(resolver_->mode_for_histogram_,
              HostResolverManager::MODE_FOR_HISTOGRAM_SYSTEM_SUPPORTS_DOH);
  }
}

TEST_F(HostResolverManagerDnsTest, TxtQuery) {
  // Simulate two separate DNS records, each with multiple strings.
  std::vector<std::string> foo_records = {"foo1", "foo2", "foo3"};
  std::vector<std::string> bar_records = {"bar1", "bar2"};
  std::vector<std::vector<std::string>> text_records = {foo_records,
                                                        bar_records};

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeTXT,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsTextResponse(
                         "host", std::move(text_records))),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());

  // Order between separate DNS records is undefined, but each record should
  // stay in order as that order may be meaningful.
  ASSERT_THAT(response.request()->GetTextResults(),
              testing::Optional(testing::UnorderedElementsAre(
                  "foo1", "foo2", "foo3", "bar1", "bar2")));
  std::vector<std::string> results =
      response.request()->GetTextResults().value();
  EXPECT_NE(results.end(), std::search(results.begin(), results.end(),
                                       foo_records.begin(), foo_records.end()));
  EXPECT_NE(results.end(), std::search(results.begin(), results.end(),
                                       bar_records.begin(), bar_records.end()));
}

TEST_F(HostResolverManagerDnsTest, TxtQuery_NonexistentDomain) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeTXT,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(MockDnsClientRule::NODOMAIN),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, TxtQuery_Failure) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypeTXT, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::FAIL), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, TxtQuery_Timeout) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypeTXT, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::TIMEOUT), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_TIMED_OUT));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, TxtQuery_Empty) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypeTXT, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::EMPTY), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, TxtQuery_Malformed) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeTXT,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(MockDnsClientRule::MALFORMED),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_MALFORMED_RESPONSE));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, TxtQuery_MismatchedName) {
  std::vector<std::vector<std::string>> text_records = {{"text"}};
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeTXT,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsTextResponse(
                         "host", std::move(text_records), "not.host")),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_MALFORMED_RESPONSE));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, TxtQuery_WrongType) {
  // Respond to a TXT query with an A response.
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeTXT,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(
                         BuildTestDnsResponse("host", IPAddress(1, 2, 3, 4))),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::TXT;

  // Responses for the wrong type should be ignored.
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

// Same as TxtQuery except we specify DNS HostResolverSource instead of relying
// on automatic determination.  Expect same results since DNS should be what we
// automatically determine, but some slightly different logic paths are
// involved.
TEST_F(HostResolverManagerDnsTest, TxtDnsQuery) {
  // Simulate two separate DNS records, each with multiple strings.
  std::vector<std::string> foo_records = {"foo1", "foo2", "foo3"};
  std::vector<std::string> bar_records = {"bar1", "bar2"};
  std::vector<std::vector<std::string>> text_records = {foo_records,
                                                        bar_records};

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeTXT,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsTextResponse(
                         "host", std::move(text_records))),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::DNS;
  parameters.dns_query_type = DnsQueryType::TXT;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());

  // Order between separate DNS records is undefined, but each record should
  // stay in order as that order may be meaningful.
  ASSERT_THAT(response.request()->GetTextResults(),
              testing::Optional(testing::UnorderedElementsAre(
                  "foo1", "foo2", "foo3", "bar1", "bar2")));
  std::vector<std::string> results =
      response.request()->GetTextResults().value();
  EXPECT_NE(results.end(), std::search(results.begin(), results.end(),
                                       foo_records.begin(), foo_records.end()));
  EXPECT_NE(results.end(), std::search(results.begin(), results.end(),
                                       bar_records.begin(), bar_records.end()));
}

TEST_F(HostResolverManagerDnsTest, PtrQuery) {
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypePTR,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsPointerResponse(
                         "host", {"foo.com", "bar.com"})),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());

  // Order between separate records is undefined.
  EXPECT_THAT(response.request()->GetHostnameResults(),
              testing::Optional(testing::UnorderedElementsAre(
                  HostPortPair("foo.com", 108), HostPortPair("bar.com", 108))));
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_Ip) {
  MockDnsClientRuleList rules;
  rules.emplace_back("8.8.8.8", dns_protocol::kTypePTR,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsPointerResponse(
                         "8.8.8.8", {"foo.com", "bar.com"})),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("8.8.8.8", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());

  // Order between separate records is undefined.
  EXPECT_THAT(response.request()->GetHostnameResults(),
              testing::Optional(testing::UnorderedElementsAre(
                  HostPortPair("foo.com", 108), HostPortPair("bar.com", 108))));
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_NonexistentDomain) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypePTR,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(MockDnsClientRule::NODOMAIN),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_Failure) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypePTR, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::FAIL), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_Timeout) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypePTR, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::TIMEOUT), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_TIMED_OUT));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_Empty) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypePTR, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::EMPTY), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_Malformed) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypePTR,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(MockDnsClientRule::MALFORMED),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_MALFORMED_RESPONSE));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_MismatchedName) {
  std::vector<std::string> ptr_records = {{"foo.com"}};
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypePTR,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsPointerResponse(
                         "host", std::move(ptr_records), "not.host")),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_MALFORMED_RESPONSE));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, PtrQuery_WrongType) {
  // Respond to a TXT query with an A response.
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypePTR,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(
                         BuildTestDnsResponse("host", IPAddress(1, 2, 3, 4))),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::PTR;

  // Responses for the wrong type should be ignored.
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

// Same as PtrQuery except we specify DNS HostResolverSource instead of relying
// on automatic determination.  Expect same results since DNS should be what we
// automatically determine, but some slightly different logic paths are
// involved.
TEST_F(HostResolverManagerDnsTest, PtrDnsQuery) {
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypePTR,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsPointerResponse(
                         "host", {"foo.com", "bar.com"})),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::DNS;
  parameters.dns_query_type = DnsQueryType::PTR;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());

  // Order between separate records is undefined.
  EXPECT_THAT(response.request()->GetHostnameResults(),
              testing::Optional(testing::UnorderedElementsAre(
                  HostPortPair("foo.com", 108), HostPortPair("bar.com", 108))));
}

TEST_F(HostResolverManagerDnsTest, SrvQuery) {
  const TestServiceRecord kRecord1 = {2, 3, 1223, "foo.com"};
  const TestServiceRecord kRecord2 = {5, 10, 80, "bar.com"};
  const TestServiceRecord kRecord3 = {5, 1, 5, "google.com"};
  const TestServiceRecord kRecord4 = {2, 100, 12345, "chromium.org"};
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeSRV,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsServiceResponse(
                         "host", {kRecord1, kRecord2, kRecord3, kRecord4})),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());

  // Expect ordered by priority, and random within a priority.
  base::Optional<std::vector<HostPortPair>> results =
      response.request()->GetHostnameResults();
  ASSERT_THAT(
      results,
      testing::Optional(testing::UnorderedElementsAre(
          HostPortPair("foo.com", 1223), HostPortPair("bar.com", 80),
          HostPortPair("google.com", 5), HostPortPair("chromium.org", 12345))));
  auto priority2 = std::vector<HostPortPair>(results.value().begin(),
                                             results.value().begin() + 2);
  EXPECT_THAT(priority2, testing::UnorderedElementsAre(
                             HostPortPair("foo.com", 1223),
                             HostPortPair("chromium.org", 12345)));
  auto priority5 = std::vector<HostPortPair>(results.value().begin() + 2,
                                             results.value().end());
  EXPECT_THAT(priority5,
              testing::UnorderedElementsAre(HostPortPair("bar.com", 80),
                                            HostPortPair("google.com", 5)));
}

// 0-weight services are allowed. Ensure that we can handle such records,
// especially the case where all entries have weight 0.
TEST_F(HostResolverManagerDnsTest, SrvQuery_ZeroWeight) {
  const TestServiceRecord kRecord1 = {5, 0, 80, "bar.com"};
  const TestServiceRecord kRecord2 = {5, 0, 5, "google.com"};
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeSRV,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsServiceResponse(
                         "host", {kRecord1, kRecord2})),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());

  // Expect ordered by priority, and random within a priority.
  EXPECT_THAT(response.request()->GetHostnameResults(),
              testing::Optional(testing::UnorderedElementsAre(
                  HostPortPair("bar.com", 80), HostPortPair("google.com", 5))));
}

TEST_F(HostResolverManagerDnsTest, SrvQuery_NonexistentDomain) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeSRV,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(MockDnsClientRule::NODOMAIN),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, SrvQuery_Failure) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypeSRV, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::FAIL), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, SrvQuery_Timeout) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypeSRV, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::TIMEOUT), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_TIMED_OUT));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, SrvQuery_Empty) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back(
      "host", dns_protocol::kTypeSRV, DnsConfig::SecureDnsMode::AUTOMATIC,
      MockDnsClientRule::Result(MockDnsClientRule::EMPTY), false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, SrvQuery_Malformed) {
  // Setup fallback to confirm it is not used for non-address results.
  set_allow_fallback_to_proctask(true);
  proc_->AddRuleForAllFamilies("host", "192.168.1.102");
  proc_->SignalMultiple(1u);

  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeSRV,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(MockDnsClientRule::MALFORMED),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_MALFORMED_RESPONSE));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, SrvQuery_MismatchedName) {
  std::vector<TestServiceRecord> srv_records = {{1, 2, 3, "foo.com"}};
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeSRV,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsServiceResponse(
                         "host", std::move(srv_records), "not.host")),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_DNS_MALFORMED_RESPONSE));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

TEST_F(HostResolverManagerDnsTest, SrvQuery_WrongType) {
  // Respond to a SRV query with an A response.
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeSRV,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(
                         BuildTestDnsResponse("host", IPAddress(1, 2, 3, 4))),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.dns_query_type = DnsQueryType::SRV;

  // Responses for the wrong type should be ignored.
  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("ok", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());
  EXPECT_FALSE(response.request()->GetHostnameResults());
}

// Same as SrvQuery except we specify DNS HostResolverSource instead of relying
// on automatic determination.  Expect same results since DNS should be what we
// automatically determine, but some slightly different logic paths are
// involved.
TEST_F(HostResolverManagerDnsTest, SrvDnsQuery) {
  const TestServiceRecord kRecord1 = {2, 3, 1223, "foo.com"};
  const TestServiceRecord kRecord2 = {5, 10, 80, "bar.com"};
  const TestServiceRecord kRecord3 = {5, 1, 5, "google.com"};
  const TestServiceRecord kRecord4 = {2, 100, 12345, "chromium.org"};
  MockDnsClientRuleList rules;
  rules.emplace_back("host", dns_protocol::kTypeSRV,
                     DnsConfig::SecureDnsMode::AUTOMATIC,
                     MockDnsClientRule::Result(BuildTestDnsServiceResponse(
                         "host", {kRecord1, kRecord2, kRecord3, kRecord4})),
                     false /* delay */);

  CreateResolver();
  UseMockDnsClient(CreateValidDnsConfig(), std::move(rules));

  HostResolver::ResolveHostParameters parameters;
  parameters.source = HostResolverSource::DNS;
  parameters.dns_query_type = DnsQueryType::SRV;

  ResolveHostResponseHelper response(resolver_->CreateRequest(
      HostPortPair("host", 108), NetLogWithSource(), parameters,
      request_context_.get(), host_cache_.get()));
  EXPECT_THAT(response.result_error(), IsOk());
  EXPECT_FALSE(response.request()->GetAddressResults());
  EXPECT_FALSE(response.request()->GetTextResults());

  // Expect ordered by priority, and random within a priority.
  base::Optional<std::vector<HostPortPair>> results =
      response.request()->GetHostnameResults();
  ASSERT_THAT(
      results,
      testing::Optional(testing::UnorderedElementsAre(
          HostPortPair("foo.com", 1223), HostPortPair("bar.com", 80),
          HostPortPair("google.com", 5), HostPortPair("chromium.org", 12345))));
  auto priority2 = std::vector<HostPortPair>(results.value().begin(),
                                             results.value().begin() + 2);
  EXPECT_THAT(priority2, testing::UnorderedElementsAre(
                             HostPortPair("foo.com", 1223),
                             HostPortPair("chromium.org", 12345)));
  auto priority5 = std::vector<HostPortPair>(results.value().begin() + 2,
                                             results.value().end());
  EXPECT_THAT(priority5,
              testing::UnorderedElementsAre(HostPortPair("bar.com", 80),
                                            HostPortPair("google.com", 5)));
}

}  // namespace net
