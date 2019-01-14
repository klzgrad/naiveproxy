// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/host_resolver_mojo.h"

#include <memory>
#include <string>
#include <utility>

#include "mojo/public/cpp/bindings/binding.h"
#include "net/base/address_list.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/base/request_priority.h"
#include "net/base/test_completion_callback.h"
#include "net/log/net_log_with_source.h"
#include "net/test/event_waiter.h"
#include "net/test/gtest_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;

namespace net {
namespace {

void Fail(int result) {
  FAIL() << "Unexpected callback called with error " << result;
}

class MockMojoHostResolverRequest {
 public:
  MockMojoHostResolverRequest(interfaces::HostResolverRequestClientPtr client,
                              const base::Closure& error_callback);
  void OnConnectionError();

 private:
  interfaces::HostResolverRequestClientPtr client_;
  const base::Closure error_callback_;
};

MockMojoHostResolverRequest::MockMojoHostResolverRequest(
    interfaces::HostResolverRequestClientPtr client,
    const base::Closure& error_callback)
    : client_(std::move(client)), error_callback_(error_callback) {
  client_.set_connection_error_handler(base::Bind(
      &MockMojoHostResolverRequest::OnConnectionError, base::Unretained(this)));
}

void MockMojoHostResolverRequest::OnConnectionError() {
  error_callback_.Run();
}

struct HostResolverAction {
  enum Action {
    COMPLETE,
    DROP,
    RETAIN,
  };

  static HostResolverAction ReturnError(Error error) {
    HostResolverAction result;
    result.error = error;
    return result;
  }

  static HostResolverAction ReturnResult(const AddressList& address_list) {
    HostResolverAction result;
    result.addresses = address_list;
    return result;
  }

  static HostResolverAction DropRequest() {
    HostResolverAction result;
    result.action = DROP;
    return result;
  }

  static HostResolverAction RetainRequest() {
    HostResolverAction result;
    result.action = RETAIN;
    return result;
  }

  Action action = COMPLETE;
  AddressList addresses;
  Error error = OK;
};

class MockMojoHostResolver : public HostResolverMojo::Impl {
 public:
  explicit MockMojoHostResolver(
      const base::Closure& request_connection_error_callback);
  ~MockMojoHostResolver() override;

  void AddAction(HostResolverAction action);

  const std::vector<HostResolver::RequestInfo>& requests() {
    return requests_received_;
  }

  void ResolveDns(std::unique_ptr<HostResolver::RequestInfo> request_info,
                  interfaces::HostResolverRequestClientPtr client) override;

 private:
  std::vector<HostResolverAction> actions_;
  size_t results_returned_ = 0;
  std::vector<HostResolver::RequestInfo> requests_received_;
  const base::Closure request_connection_error_callback_;
  std::vector<std::unique_ptr<MockMojoHostResolverRequest>> requests_;
};

MockMojoHostResolver::MockMojoHostResolver(
    const base::Closure& request_connection_error_callback)
    : request_connection_error_callback_(request_connection_error_callback) {
}

MockMojoHostResolver::~MockMojoHostResolver() {
  EXPECT_EQ(results_returned_, actions_.size());
}

void MockMojoHostResolver::AddAction(HostResolverAction action) {
  actions_.push_back(std::move(action));
}

void MockMojoHostResolver::ResolveDns(
    std::unique_ptr<HostResolver::RequestInfo> request_info,
    interfaces::HostResolverRequestClientPtr client) {
  requests_received_.push_back(std::move(*request_info));
  ASSERT_LE(results_returned_, actions_.size());
  switch (actions_[results_returned_].action) {
    case HostResolverAction::COMPLETE:
      client->ReportResult(actions_[results_returned_].error,
                           std::move(actions_[results_returned_].addresses));
      break;
    case HostResolverAction::RETAIN:
      requests_.push_back(std::make_unique<MockMojoHostResolverRequest>(
          std::move(client), request_connection_error_callback_));
      break;
    case HostResolverAction::DROP:
      client.reset();
      break;
  }
  results_returned_++;
}

}  // namespace

class HostResolverMojoTest : public TestWithScopedTaskEnvironment {
 protected:
  enum class ConnectionErrorSource {
    REQUEST,
  };
  using Waiter = EventWaiter<ConnectionErrorSource>;

  void SetUp() override {
    mock_resolver_.reset(new MockMojoHostResolver(
        base::Bind(&Waiter::NotifyEvent, base::Unretained(&waiter_),
                   ConnectionErrorSource::REQUEST)));
    resolver_.reset(new HostResolverMojo(mock_resolver_.get()));
  }

  int Resolve(const HostResolver::RequestInfo& request_info,
              AddressList* result) {
    TestCompletionCallback callback;
    return callback.GetResult(
        resolver_->Resolve(request_info, DEFAULT_PRIORITY, result,
                           callback.callback(), &request_, NetLogWithSource()));
  }

  std::unique_ptr<MockMojoHostResolver> mock_resolver_;

  std::unique_ptr<HostResolverMojo> resolver_;

  std::unique_ptr<HostResolver::Request> request_;

  Waiter waiter_;
};

TEST_F(HostResolverMojoTest, Basic) {
  AddressList address_list;
  IPAddress address(1, 2, 3, 4);
  address_list.push_back(IPEndPoint(address, 12345));
  address_list.push_back(
      IPEndPoint(ConvertIPv4ToIPv4MappedIPv6(address), 12345));
  mock_resolver_->AddAction(HostResolverAction::ReturnResult(address_list));
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:12345"));
  AddressList result;
  EXPECT_THAT(Resolve(request_info, &result), IsOk());
  ASSERT_EQ(2u, result.size());
  EXPECT_EQ(address_list[0], result[0]);
  EXPECT_EQ(address_list[1], result[1]);

  ASSERT_EQ(1u, mock_resolver_->requests().size());
  const HostResolver::RequestInfo& request = mock_resolver_->requests()[0];
  EXPECT_EQ("example.com", request.hostname());
  EXPECT_EQ(12345, request.port());
  EXPECT_EQ(ADDRESS_FAMILY_UNSPECIFIED, request.address_family());
  EXPECT_FALSE(request.is_my_ip_address());
}

TEST_F(HostResolverMojoTest, ResolveCachedResult) {
  AddressList address_list;
  IPAddress address(1, 2, 3, 4);
  address_list.push_back(IPEndPoint(address, 12345));
  address_list.push_back(
      IPEndPoint(ConvertIPv4ToIPv4MappedIPv6(address), 12345));
  mock_resolver_->AddAction(HostResolverAction::ReturnResult(address_list));
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:12345"));
  AddressList result;
  ASSERT_THAT(Resolve(request_info, &result), IsOk());
  ASSERT_EQ(1u, mock_resolver_->requests().size());

  result.clear();
  request_info.set_host_port_pair(HostPortPair::FromString("example.com:6789"));
  EXPECT_THAT(Resolve(request_info, &result), IsOk());
  ASSERT_EQ(2u, result.size());
  address_list.clear();
  address_list.push_back(IPEndPoint(address, 6789));
  address_list.push_back(
      IPEndPoint(ConvertIPv4ToIPv4MappedIPv6(address), 6789));
  EXPECT_EQ(address_list[0], result[0]);
  EXPECT_EQ(address_list[1], result[1]);
  EXPECT_EQ(1u, mock_resolver_->requests().size());

  mock_resolver_->AddAction(HostResolverAction::ReturnResult(address_list));
  result.clear();
  request_info.set_allow_cached_response(false);
  EXPECT_THAT(Resolve(request_info, &result), IsOk());
  ASSERT_EQ(2u, result.size());
  EXPECT_EQ(address_list[0], result[0]);
  EXPECT_EQ(address_list[1], result[1]);
  EXPECT_EQ(2u, mock_resolver_->requests().size());
}

TEST_F(HostResolverMojoTest, Multiple) {
  AddressList address_list;
  IPAddress address(1, 2, 3, 4);
  address_list.push_back(IPEndPoint(address, 12345));
  mock_resolver_->AddAction(HostResolverAction::ReturnResult(address_list));
  mock_resolver_->AddAction(
      HostResolverAction::ReturnError(ERR_NAME_NOT_RESOLVED));
  HostResolver::RequestInfo request_info1(
      HostPortPair::FromString("example.com:12345"));
  request_info1.set_address_family(ADDRESS_FAMILY_IPV4);
  request_info1.set_is_my_ip_address(true);
  HostResolver::RequestInfo request_info2(
      HostPortPair::FromString("example.org:80"));
  request_info2.set_address_family(ADDRESS_FAMILY_IPV6);
  AddressList result1;
  AddressList result2;
  std::unique_ptr<HostResolver::Request> request1;
  std::unique_ptr<HostResolver::Request> request2;
  TestCompletionCallback callback1;
  TestCompletionCallback callback2;
  ASSERT_EQ(ERR_IO_PENDING, resolver_->Resolve(request_info1, DEFAULT_PRIORITY,
                                               &result1, callback1.callback(),
                                               &request1, NetLogWithSource()));
  ASSERT_EQ(ERR_IO_PENDING, resolver_->Resolve(request_info2, DEFAULT_PRIORITY,
                                               &result2, callback2.callback(),
                                               &request2, NetLogWithSource()));
  EXPECT_THAT(callback1.GetResult(ERR_IO_PENDING), IsOk());
  EXPECT_THAT(callback2.GetResult(ERR_IO_PENDING),
              IsError(ERR_NAME_NOT_RESOLVED));
  ASSERT_EQ(1u, result1.size());
  EXPECT_EQ(address_list[0], result1[0]);
  ASSERT_EQ(0u, result2.size());

  ASSERT_EQ(2u, mock_resolver_->requests().size());
  const HostResolver::RequestInfo& info1 = mock_resolver_->requests()[0];
  EXPECT_EQ("example.com", info1.hostname());
  EXPECT_EQ(12345, info1.port());
  EXPECT_EQ(ADDRESS_FAMILY_IPV4, info1.address_family());
  EXPECT_TRUE(info1.is_my_ip_address());
  const HostResolver::RequestInfo& info2 = mock_resolver_->requests()[1];
  EXPECT_EQ("example.org", info2.hostname());
  EXPECT_EQ(80, info2.port());
  EXPECT_EQ(ADDRESS_FAMILY_IPV6, info2.address_family());
  EXPECT_FALSE(info2.is_my_ip_address());
}

TEST_F(HostResolverMojoTest, Error) {
  mock_resolver_->AddAction(
      HostResolverAction::ReturnError(ERR_NAME_NOT_RESOLVED));
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:8080"));
  request_info.set_address_family(ADDRESS_FAMILY_IPV4);
  AddressList result;
  EXPECT_THAT(Resolve(request_info, &result), IsError(ERR_NAME_NOT_RESOLVED));
  EXPECT_TRUE(result.empty());

  ASSERT_EQ(1u, mock_resolver_->requests().size());
  const HostResolver::RequestInfo& request = mock_resolver_->requests()[0];
  EXPECT_EQ("example.com", request.hostname());
  EXPECT_EQ(8080, request.port());
  EXPECT_EQ(ADDRESS_FAMILY_IPV4, request.address_family());
  EXPECT_FALSE(request.is_my_ip_address());
}

TEST_F(HostResolverMojoTest, EmptyResult) {
  mock_resolver_->AddAction(HostResolverAction::ReturnError(OK));
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:8080"));
  AddressList result;
  EXPECT_THAT(Resolve(request_info, &result), IsOk());
  EXPECT_TRUE(result.empty());

  ASSERT_EQ(1u, mock_resolver_->requests().size());
}

TEST_F(HostResolverMojoTest, Cancel) {
  mock_resolver_->AddAction(HostResolverAction::RetainRequest());
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:80"));
  request_info.set_address_family(ADDRESS_FAMILY_IPV6);
  AddressList result;
  std::unique_ptr<HostResolver::Request> request;
  resolver_->Resolve(request_info, DEFAULT_PRIORITY, &result, base::Bind(&Fail),
                     &request, NetLogWithSource());
  request.reset();
  waiter_.WaitForEvent(ConnectionErrorSource::REQUEST);
  EXPECT_TRUE(result.empty());

  ASSERT_EQ(1u, mock_resolver_->requests().size());
  const HostResolver::RequestInfo& info1 = mock_resolver_->requests()[0];
  EXPECT_EQ("example.com", info1.hostname());
  EXPECT_EQ(80, info1.port());
  EXPECT_EQ(ADDRESS_FAMILY_IPV6, info1.address_family());
  EXPECT_FALSE(info1.is_my_ip_address());
}

TEST_F(HostResolverMojoTest, ImplDropsClientConnection) {
  mock_resolver_->AddAction(HostResolverAction::DropRequest());
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:1"));
  AddressList result;
  EXPECT_THAT(Resolve(request_info, &result), IsError(ERR_FAILED));
  EXPECT_TRUE(result.empty());

  ASSERT_EQ(1u, mock_resolver_->requests().size());
  const HostResolver::RequestInfo& info2 = mock_resolver_->requests()[0];
  EXPECT_EQ("example.com", info2.hostname());
  EXPECT_EQ(1, info2.port());
  EXPECT_EQ(ADDRESS_FAMILY_UNSPECIFIED, info2.address_family());
  EXPECT_FALSE(info2.is_my_ip_address());
}

TEST_F(HostResolverMojoTest, ResolveFromCache_Miss) {
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:8080"));
  AddressList result;
  EXPECT_EQ(ERR_DNS_CACHE_MISS, resolver_->ResolveFromCache(
                                    request_info, &result, NetLogWithSource()));
  EXPECT_TRUE(result.empty());
}

TEST_F(HostResolverMojoTest, ResolveFromCache_Hit) {
  AddressList address_list;
  IPAddress address(1, 2, 3, 4);
  address_list.push_back(IPEndPoint(address, 12345));
  address_list.push_back(
      IPEndPoint(ConvertIPv4ToIPv4MappedIPv6(address), 12345));
  mock_resolver_->AddAction(HostResolverAction::ReturnResult(address_list));
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:12345"));
  AddressList result;
  ASSERT_THAT(Resolve(request_info, &result), IsOk());
  EXPECT_EQ(1u, mock_resolver_->requests().size());

  result.clear();
  EXPECT_EQ(OK, resolver_->ResolveFromCache(request_info, &result,
                                            NetLogWithSource()));
  ASSERT_EQ(2u, result.size());
  EXPECT_EQ(address_list[0], result[0]);
  EXPECT_EQ(address_list[1], result[1]);
  EXPECT_EQ(1u, mock_resolver_->requests().size());
}

TEST_F(HostResolverMojoTest, ResolveFromCache_CacheNotAllowed) {
  AddressList address_list;
  IPAddress address(1, 2, 3, 4);
  address_list.push_back(IPEndPoint(address, 12345));
  address_list.push_back(
      IPEndPoint(ConvertIPv4ToIPv4MappedIPv6(address), 12345));
  mock_resolver_->AddAction(HostResolverAction::ReturnResult(address_list));
  HostResolver::RequestInfo request_info(
      HostPortPair::FromString("example.com:12345"));
  AddressList result;
  ASSERT_THAT(Resolve(request_info, &result), IsOk());
  EXPECT_EQ(1u, mock_resolver_->requests().size());

  result.clear();
  request_info.set_allow_cached_response(false);
  EXPECT_EQ(ERR_DNS_CACHE_MISS, resolver_->ResolveFromCache(
                                    request_info, &result, NetLogWithSource()));
  EXPECT_TRUE(result.empty());
}

TEST_F(HostResolverMojoTest, GetHostCache) {
  EXPECT_TRUE(resolver_->GetHostCache());
}

}  // namespace net
