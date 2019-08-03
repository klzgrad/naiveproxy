// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/url_request/url_request_context_builder.h"

#include "base/run_loop.h"
#include "build/build_config.h"
#include "net/base/request_priority.h"
#include "net/dns/host_resolver.h"
#include "net/dns/host_resolver_manager.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_auth_challenge_tokenizer.h"
#include "net/http/http_auth_handler.h"
#include "net/http/http_auth_handler_factory.h"
#include "net/log/net_log_with_source.h"
#include "net/ssl/ssl_info.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

#if defined(OS_LINUX) || defined(OS_ANDROID)
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#endif  // defined(OS_LINUX) || defined(OS_ANDROID)

#if BUILDFLAG(ENABLE_REPORTING)
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_policy.h"
#include "net/reporting/reporting_service.h"
#include "net/reporting/reporting_uploader.h"
#endif  // BUILDFLAG(ENABLE_REPORTING)

namespace net {

namespace {

class MockHttpAuthHandlerFactory : public HttpAuthHandlerFactory {
 public:
  MockHttpAuthHandlerFactory(std::string supported_scheme, int return_code)
      : return_code_(return_code), supported_scheme_(supported_scheme) {}
  ~MockHttpAuthHandlerFactory() override = default;

  int CreateAuthHandler(HttpAuthChallengeTokenizer* challenge,
                        HttpAuth::Target target,
                        const SSLInfo& ssl_info,
                        const GURL& origin,
                        CreateReason reason,
                        int nonce_count,
                        const NetLogWithSource& net_log,
                        HostResolver* host_resolver,
                        std::unique_ptr<HttpAuthHandler>* handler) override {
    handler->reset();

    return challenge->scheme() == supported_scheme_
               ? return_code_
               : ERR_UNSUPPORTED_AUTH_SCHEME;
  }

 private:
  int return_code_;
  std::string supported_scheme_;
};

class URLRequestContextBuilderTest : public PlatformTest,
                                     public WithScopedTaskEnvironment {
 protected:
  URLRequestContextBuilderTest() {
    test_server_.AddDefaultHandlers(
        base::FilePath(FILE_PATH_LITERAL("net/data/url_request_unittest")));
#if defined(OS_LINUX) || defined(OS_ANDROID)
    builder_.set_proxy_config_service(std::make_unique<ProxyConfigServiceFixed>(
        ProxyConfigWithAnnotation::CreateDirect()));
#endif  // defined(OS_LINUX) || defined(OS_ANDROID)
  }

  std::unique_ptr<HostResolver> host_resolver_ =
      std::make_unique<MockHostResolver>();
  EmbeddedTestServer test_server_;
  URLRequestContextBuilder builder_;
};

TEST_F(URLRequestContextBuilderTest, DefaultSettings) {
  ASSERT_TRUE(test_server_.Start());

  std::unique_ptr<URLRequestContext> context(builder_.Build());
  TestDelegate delegate;
  std::unique_ptr<URLRequest> request(context->CreateRequest(
      test_server_.GetURL("/echoheader?Foo"), DEFAULT_PRIORITY, &delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS));
  request->set_method("GET");
  request->SetExtraRequestHeaderByName("Foo", "Bar", false);
  request->Start();
  base::RunLoop().Run();
  EXPECT_EQ("Bar", delegate.data_received());
}

TEST_F(URLRequestContextBuilderTest, UserAgent) {
  ASSERT_TRUE(test_server_.Start());

  builder_.set_user_agent("Bar");
  std::unique_ptr<URLRequestContext> context(builder_.Build());
  TestDelegate delegate;
  std::unique_ptr<URLRequest> request(context->CreateRequest(
      test_server_.GetURL("/echoheader?User-Agent"), DEFAULT_PRIORITY,
      &delegate, TRAFFIC_ANNOTATION_FOR_TESTS));
  request->set_method("GET");
  request->Start();
  base::RunLoop().Run();
  EXPECT_EQ("Bar", delegate.data_received());
}

TEST_F(URLRequestContextBuilderTest, DefaultHttpAuthHandlerFactory) {
  GURL gurl("www.google.com");
  std::unique_ptr<HttpAuthHandler> handler;
  std::unique_ptr<URLRequestContext> context(builder_.Build());
  SSLInfo null_ssl_info;

  // Verify that the default basic handler is present
  EXPECT_EQ(OK,
            context->http_auth_handler_factory()->CreateAuthHandlerFromString(
                "basic", HttpAuth::AUTH_SERVER, null_ssl_info, gurl,
                NetLogWithSource(), host_resolver_.get(), &handler));
}

TEST_F(URLRequestContextBuilderTest, CustomHttpAuthHandlerFactory) {
  GURL gurl("www.google.com");
  const int kBasicReturnCode = OK;
  std::unique_ptr<HttpAuthHandler> handler;
  builder_.SetHttpAuthHandlerFactory(
      std::make_unique<MockHttpAuthHandlerFactory>("ExtraScheme",
                                                   kBasicReturnCode));
  std::unique_ptr<URLRequestContext> context(builder_.Build());
  SSLInfo null_ssl_info;
  // Verify that a handler is returned for a custom scheme.
  EXPECT_EQ(kBasicReturnCode,
            context->http_auth_handler_factory()->CreateAuthHandlerFromString(
                "ExtraScheme", HttpAuth::AUTH_SERVER, null_ssl_info, gurl,
                NetLogWithSource(), host_resolver_.get(), &handler));

  // Verify that the default basic handler isn't present
  EXPECT_EQ(ERR_UNSUPPORTED_AUTH_SCHEME,
            context->http_auth_handler_factory()->CreateAuthHandlerFromString(
                "basic", HttpAuth::AUTH_SERVER, null_ssl_info, gurl,
                NetLogWithSource(), host_resolver_.get(), &handler));

  // Verify that a handler isn't returned for a bogus scheme.
  EXPECT_EQ(ERR_UNSUPPORTED_AUTH_SCHEME,
            context->http_auth_handler_factory()->CreateAuthHandlerFromString(
                "Bogus", HttpAuth::AUTH_SERVER, null_ssl_info, gurl,
                NetLogWithSource(), host_resolver_.get(), &handler));
}

#if BUILDFLAG(ENABLE_REPORTING)
// See crbug.com/935209. This test ensures that shutdown occurs correctly and
// does not crash while destoying the NEL and Reporting services in the process
// of destroying the URLRequestContext whilst Reporting has a pending upload.
TEST_F(URLRequestContextBuilderTest, ShutDownNELAndReportingWithPendingUpload) {
  std::unique_ptr<MockHostResolver> host_resolver =
      std::make_unique<MockHostResolver>();
  host_resolver->set_ondemand_mode(true);
  MockHostResolver* mock_host_resolver = host_resolver.get();
  builder_.set_host_resolver(std::move(host_resolver));
  builder_.set_proxy_resolution_service(ProxyResolutionService::CreateDirect());
  builder_.set_reporting_policy(std::make_unique<ReportingPolicy>());
  builder_.set_network_error_logging_enabled(true);

  std::unique_ptr<URLRequestContext> context(builder_.Build());
  ASSERT_TRUE(context->network_error_logging_service());
  ASSERT_TRUE(context->reporting_service());

  // Queue a pending upload.
  GURL url("https://www.foo.test");
  context->reporting_service()->GetContextForTesting()->uploader()->StartUpload(
      url::Origin::Create(url), url, "report body", 0, base::DoNothing());
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1, context->reporting_service()
                   ->GetContextForTesting()
                   ->uploader()
                   ->GetPendingUploadCountForTesting());
  ASSERT_TRUE(mock_host_resolver->has_pending_requests());

  // This should shut down and destroy the NEL and Reporting services, including
  // the PendingUpload, and should not cause a crash.
  context.reset();
}
#endif  // BUILDFLAG(ENABLE_REPORTING)

TEST_F(URLRequestContextBuilderTest, DefaultHostResolver) {
  auto manager = std::make_unique<HostResolverManager>(
      HostResolver::ManagerOptions(), nullptr);

  builder_.set_host_resolver_manager(manager.get());
  std::unique_ptr<URLRequestContext> context = builder_.Build();

  EXPECT_EQ(context.get(), context->host_resolver()->GetContextForTesting());
  EXPECT_EQ(manager.get(), context->host_resolver()->GetManagerForTesting());
}

TEST_F(URLRequestContextBuilderTest, CustomHostResolver) {
  std::unique_ptr<HostResolver> resolver =
      HostResolver::CreateStandaloneResolver(nullptr);
  ASSERT_FALSE(resolver->GetContextForTesting());

  builder_.set_host_resolver(std::move(resolver));
  std::unique_ptr<URLRequestContext> context = builder_.Build();

  EXPECT_EQ(context.get(), context->host_resolver()->GetContextForTesting());
}

}  // namespace

}  // namespace net
