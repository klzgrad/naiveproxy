// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mojo_host_resolver_impl.h"

#include <string>
#include <utility>

#include "base/callback_helpers.h"
#include "base/run_loop.h"
#include "base/time/time.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "net/base/address_list.h"
#include "net/base/net_errors.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/gtest_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;

namespace net {

namespace {

class TestRequestClient : public interfaces::HostResolverRequestClient {
 public:
  explicit TestRequestClient(
      mojo::InterfaceRequest<interfaces::HostResolverRequestClient> req)
      : done_(false), binding_(this, std::move(req)) {
    binding_.set_connection_error_handler(base::Bind(
        &TestRequestClient::OnConnectionError, base::Unretained(this)));
  }

  void WaitForResult();
  void WaitForConnectionError();

  int32_t error_;
  AddressList results_;

 private:
  // Overridden from interfaces::HostResolverRequestClient.
  void ReportResult(int32_t error, const AddressList& results) override;

  // Mojo error handler.
  void OnConnectionError();

  bool done_;
  base::Closure run_loop_quit_closure_;
  base::Closure connection_error_quit_closure_;

  mojo::Binding<interfaces::HostResolverRequestClient> binding_;
};

void TestRequestClient::WaitForResult() {
  if (done_)
    return;

  base::RunLoop run_loop;
  run_loop_quit_closure_ = run_loop.QuitClosure();
  run_loop.Run();
  ASSERT_TRUE(done_);
}

void TestRequestClient::WaitForConnectionError() {
  base::RunLoop run_loop;
  connection_error_quit_closure_ = run_loop.QuitClosure();
  run_loop.Run();
}

void TestRequestClient::ReportResult(int32_t error,
                                     const AddressList& results) {
  if (!run_loop_quit_closure_.is_null()) {
    run_loop_quit_closure_.Run();
  }
  ASSERT_FALSE(done_);
  error_ = error;
  results_ = results;
  done_ = true;
}

void TestRequestClient::OnConnectionError() {
  if (!connection_error_quit_closure_.is_null())
    connection_error_quit_closure_.Run();
}

class CallbackMockHostResolver : public MockHostResolver {
 public:
  CallbackMockHostResolver() = default;
  ~CallbackMockHostResolver() override = default;

  // Set a callback to run whenever Resolve is called. Callback is cleared after
  // every run.
  void SetResolveCallback(base::Closure callback) {
    resolve_callback_ = callback;
  }

  // Overridden from MockHostResolver.
  int Resolve(const RequestInfo& info,
              RequestPriority priority,
              AddressList* addresses,
              CompletionOnceCallback callback,
              std::unique_ptr<Request>* request,
              const NetLogWithSource& net_log) override;

 private:
  base::Closure resolve_callback_;
};

int CallbackMockHostResolver::Resolve(const RequestInfo& info,
                                      RequestPriority priority,
                                      AddressList* addresses,
                                      CompletionOnceCallback callback,
                                      std::unique_ptr<Request>* request,
                                      const NetLogWithSource& net_log) {
  int result = MockHostResolver::Resolve(info, priority, addresses,
                                         std::move(callback), request, net_log);
  if (!resolve_callback_.is_null()) {
    std::move(resolve_callback_).Run();
  }
  return result;
}

}  // namespace

class MojoHostResolverImplTest : public TestWithScopedTaskEnvironment {
 protected:
  void SetUp() override {
    mock_host_resolver_.rules()->AddRule("example.com", "1.2.3.4");
    mock_host_resolver_.rules()->AddRule("chromium.org", "8.8.8.8");
    mock_host_resolver_.rules()->AddSimulatedFailure("failure.fail");

    resolver_service_.reset(
        new MojoHostResolverImpl(&mock_host_resolver_, NetLogWithSource()));
  }

  std::unique_ptr<HostResolver::RequestInfo>
  CreateRequest(const std::string& host, uint16_t port, bool is_my_ip_address) {
    std::unique_ptr<HostResolver::RequestInfo> request =
        std::make_unique<HostResolver::RequestInfo>(HostPortPair(host, port));
    request->set_is_my_ip_address(is_my_ip_address);
    request->set_address_family(ADDRESS_FAMILY_IPV4);
    return request;
  }

  // Wait until the mock resolver has received |num| resolve requests.
  void WaitForRequests(size_t num) {
    while (mock_host_resolver_.num_resolve() < num) {
      base::RunLoop run_loop;
      mock_host_resolver_.SetResolveCallback(run_loop.QuitClosure());
      run_loop.Run();
    }
  }

  CallbackMockHostResolver mock_host_resolver_;
  std::unique_ptr<MojoHostResolverImpl> resolver_service_;
};

TEST_F(MojoHostResolverImplTest, Resolve) {
  interfaces::HostResolverRequestClientPtr client_ptr;
  TestRequestClient client(mojo::MakeRequest(&client_ptr));

  resolver_service_->Resolve(CreateRequest("example.com", 80, false),
                             std::move(client_ptr));
  client.WaitForResult();

  EXPECT_THAT(client.error_, IsOk());
  AddressList& address_list = client.results_;
  EXPECT_EQ(1U, address_list.size());
  EXPECT_EQ("1.2.3.4:80", address_list[0].ToString());
}

TEST_F(MojoHostResolverImplTest, ResolveSynchronous) {
  interfaces::HostResolverRequestClientPtr client_ptr;
  TestRequestClient client(mojo::MakeRequest(&client_ptr));

  mock_host_resolver_.set_synchronous_mode(true);

  resolver_service_->Resolve(CreateRequest("example.com", 80, false),
                             std::move(client_ptr));
  client.WaitForResult();

  EXPECT_THAT(client.error_, IsOk());
  AddressList& address_list = client.results_;
  EXPECT_EQ(1U, address_list.size());
  EXPECT_EQ("1.2.3.4:80", address_list[0].ToString());
}

TEST_F(MojoHostResolverImplTest, ResolveMultiple) {
  interfaces::HostResolverRequestClientPtr client1_ptr;
  TestRequestClient client1(mojo::MakeRequest(&client1_ptr));
  interfaces::HostResolverRequestClientPtr client2_ptr;
  TestRequestClient client2(mojo::MakeRequest(&client2_ptr));

  mock_host_resolver_.set_ondemand_mode(true);

  resolver_service_->Resolve(CreateRequest("example.com", 80, false),
                             std::move(client1_ptr));
  resolver_service_->Resolve(CreateRequest("chromium.org", 80, false),
                             std::move(client2_ptr));
  WaitForRequests(2);
  mock_host_resolver_.ResolveAllPending();

  client1.WaitForResult();
  client2.WaitForResult();

  EXPECT_THAT(client1.error_, IsOk());
  AddressList& address_list1 = client1.results_;
  EXPECT_EQ(1U, address_list1.size());
  EXPECT_EQ("1.2.3.4:80", address_list1[0].ToString());
  EXPECT_THAT(client2.error_, IsOk());
  AddressList& address_list2 = client2.results_;
  EXPECT_EQ(1U, address_list2.size());
  EXPECT_EQ("8.8.8.8:80", address_list2[0].ToString());
}

TEST_F(MojoHostResolverImplTest, ResolveDuplicate) {
  interfaces::HostResolverRequestClientPtr client1_ptr;
  TestRequestClient client1(mojo::MakeRequest(&client1_ptr));
  interfaces::HostResolverRequestClientPtr client2_ptr;
  TestRequestClient client2(mojo::MakeRequest(&client2_ptr));

  mock_host_resolver_.set_ondemand_mode(true);

  resolver_service_->Resolve(CreateRequest("example.com", 80, false),
                             std::move(client1_ptr));
  resolver_service_->Resolve(CreateRequest("example.com", 80, false),
                             std::move(client2_ptr));
  WaitForRequests(2);
  mock_host_resolver_.ResolveAllPending();

  client1.WaitForResult();
  client2.WaitForResult();

  EXPECT_THAT(client1.error_, IsOk());
  AddressList& address_list1 = client1.results_;
  EXPECT_EQ(1U, address_list1.size());
  EXPECT_EQ("1.2.3.4:80", address_list1[0].ToString());
  EXPECT_THAT(client2.error_, IsOk());
  AddressList& address_list2 = client2.results_;
  EXPECT_EQ(1U, address_list2.size());
  EXPECT_EQ("1.2.3.4:80", address_list2[0].ToString());
}

TEST_F(MojoHostResolverImplTest, ResolveFailure) {
  interfaces::HostResolverRequestClientPtr client_ptr;
  TestRequestClient client(mojo::MakeRequest(&client_ptr));

  resolver_service_->Resolve(CreateRequest("failure.fail", 80, false),
                             std::move(client_ptr));
  client.WaitForResult();

  EXPECT_THAT(client.error_, IsError(net::ERR_NAME_NOT_RESOLVED));
  EXPECT_TRUE(client.results_.empty());
}

TEST_F(MojoHostResolverImplTest, DestroyClient) {
  interfaces::HostResolverRequestClientPtr client_ptr;
  std::unique_ptr<TestRequestClient> client(
      new TestRequestClient(mojo::MakeRequest(&client_ptr)));

  mock_host_resolver_.set_ondemand_mode(true);

  resolver_service_->Resolve(CreateRequest("example.com", 80, false),
                             std::move(client_ptr));
  WaitForRequests(1);

  client.reset();
  base::RunLoop().RunUntilIdle();

  mock_host_resolver_.ResolveAllPending();
  base::RunLoop().RunUntilIdle();
}

}  // namespace net
