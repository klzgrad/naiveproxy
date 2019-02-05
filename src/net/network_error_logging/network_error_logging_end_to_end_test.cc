// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/stringprintf.h"
#include "base/test/values_test_util.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "net/base/net_errors.h"
#include "net/network_error_logging/network_error_logging_service.h"
#include "net/reporting/reporting_policy.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "net/url_request/url_request_status.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

#if defined(OS_LINUX) || defined(OS_ANDROID)
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#endif  // defined(OS_LINUX) || defined(OS_ANDROID)

namespace net {
namespace {

const char kGroup[] = "network-errors";
const int kMaxAgeSec = 86400;

const char kConfigurePath[] = "/configure";
const char kFailPath[] = "/fail";
const char kReportPath[] = "/report";

class HungHttpResponse : public test_server::HttpResponse {
 public:
  HungHttpResponse() = default;

  void SendResponse(const test_server::SendBytesCallback& send,
                    const test_server::SendCompleteCallback& done) override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(HungHttpResponse);
};

class NetworkErrorLoggingEndToEndTest : public TestWithScopedTaskEnvironment {
 protected:
  NetworkErrorLoggingEndToEndTest()
      : main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
        test_server_(test_server::EmbeddedTestServer::TYPE_HTTPS),
        upload_should_hang_(false),
        upload_received_(false) {
    // Make report delivery happen instantly.
    auto policy = ReportingPolicy::Create();
    policy->delivery_interval = base::TimeDelta::FromSeconds(0);

    URLRequestContextBuilder builder;
#if defined(OS_LINUX) || defined(OS_ANDROID)
    builder.set_proxy_config_service(std::make_unique<ProxyConfigServiceFixed>(
        ProxyConfigWithAnnotation::CreateDirect()));
#endif  // defined(OS_LINUX) || defined(OS_ANDROID)
    builder.set_reporting_policy(std::move(policy));
    builder.set_network_error_logging_enabled(true);
    url_request_context_ = builder.Build();

    EXPECT_TRUE(url_request_context_->reporting_service());
    EXPECT_TRUE(url_request_context_->network_error_logging_service());

    test_server_.RegisterRequestHandler(base::BindRepeating(
        &NetworkErrorLoggingEndToEndTest::HandleConfigureRequest,
        base::Unretained(this)));
    test_server_.RegisterRequestHandler(
        base::BindRepeating(&NetworkErrorLoggingEndToEndTest::HandleFailRequest,
                            base::Unretained(this)));
    test_server_.RegisterRequestHandler(base::BindRepeating(
        &NetworkErrorLoggingEndToEndTest::HandleReportRequest,
        base::Unretained(this)));
    EXPECT_TRUE(test_server_.Start());
  }

  ~NetworkErrorLoggingEndToEndTest() override {
    EXPECT_TRUE(test_server_.ShutdownAndWaitUntilComplete());
  }

  GURL GetConfigureURL() { return test_server_.GetURL(kConfigurePath); }

  GURL GetFailURL() { return test_server_.GetURL(kFailPath); }

  GURL GetReportURL() { return test_server_.GetURL(kReportPath); }

  std::unique_ptr<test_server::HttpResponse> HandleConfigureRequest(
      const test_server::HttpRequest& request) {
    if (request.relative_url != kConfigurePath)
      return nullptr;

    GURL endpoint_url = GetReportURL();

    auto response = std::make_unique<test_server::BasicHttpResponse>();
    response->AddCustomHeader(
        "Report-To",
        base::StringPrintf("{\"endpoints\":[{\"url\":\"%s\"}],\"group\":\"%s\","
                           "\"max_age\":%d}",
                           endpoint_url.spec().c_str(), kGroup, kMaxAgeSec));
    response->AddCustomHeader(
        "NEL", base::StringPrintf("{\"report_to\":\"%s\",\"max_age\":%d}",
                                  kGroup, kMaxAgeSec));
    response->set_content_type("text/plain");
    response->set_content("");
    return std::move(response);
  }

  std::unique_ptr<test_server::HttpResponse> HandleFailRequest(
      const test_server::HttpRequest& request) {
    if (request.relative_url != kFailPath)
      return nullptr;

    return std::make_unique<test_server::RawHttpResponse>("", "");
  }

  std::unique_ptr<test_server::HttpResponse> HandleReportRequest(
      const test_server::HttpRequest& request) {
    if (request.relative_url != kReportPath)
      return nullptr;

    EXPECT_TRUE(request.has_content);
    main_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&NetworkErrorLoggingEndToEndTest::OnUploadReceived,
                       base::Unretained(this), request.content));

    if (upload_should_hang_)
      return std::make_unique<HungHttpResponse>();

    auto response = std::make_unique<test_server::BasicHttpResponse>();
    response->set_content_type("text/plain");
    response->set_content("");
    return std::move(response);
  }

  void OnUploadReceived(std::string content) {
    upload_received_ = true;
    upload_content_ = content;
    upload_run_loop_.Quit();
  }

  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
  std::unique_ptr<URLRequestContext> url_request_context_;
  test_server::EmbeddedTestServer test_server_;

  bool upload_should_hang_;
  bool upload_received_;
  std::string upload_content_;
  base::RunLoop upload_run_loop_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkErrorLoggingEndToEndTest);
};

#if defined(OS_WIN)
// TODO(https://crbug.com/829650): Fix and re-enable these tests.
#define MAYBE_ReportNetworkError DISABLED_ReportNetworkError
#else
#define MAYBE_ReportNetworkError ReportNetworkError
#endif
TEST_F(NetworkErrorLoggingEndToEndTest, MAYBE_ReportNetworkError) {
  TestDelegate configure_delegate;
  configure_delegate.set_on_complete(base::DoNothing());
  auto configure_request = url_request_context_->CreateRequest(
      GetConfigureURL(), DEFAULT_PRIORITY, &configure_delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  configure_request->set_method("GET");
  configure_request->Start();

  TestDelegate fail_delegate;
  fail_delegate.set_on_complete(base::DoNothing());
  auto fail_request = url_request_context_->CreateRequest(
      GetFailURL(), DEFAULT_PRIORITY, &fail_delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  fail_request->set_method("GET");
  fail_request->Start();

  upload_run_loop_.Run();

  EXPECT_TRUE(upload_received_);
  auto reports = base::test::ParseJson(upload_content_);

  base::ListValue* reports_list;
  ASSERT_TRUE(reports->GetAsList(&reports_list));
  ASSERT_EQ(1u, reports_list->GetSize());
  base::DictionaryValue* report_dict;
  ASSERT_TRUE(reports_list->GetDictionary(0u, &report_dict));

  ExpectDictStringValue("network-error", *report_dict, "type");
  ExpectDictStringValue(GetFailURL().spec(), *report_dict, "url");
  base::DictionaryValue* body_dict;
  ASSERT_TRUE(report_dict->GetDictionary("body", &body_dict));

  ExpectDictStringValue("http.response.empty", *body_dict, "type");
  ExpectDictIntegerValue(0, *body_dict, "status_code");
}

#if defined(OS_WIN)
// TODO(https://crbug.com/829650): Fix and re-enable these tests.
#define MAYBE_UploadAtShutdown DISABLED_UploadAtShutdown
#else
#define MAYBE_UploadAtShutdown UploadAtShutdown
#endif
// Make sure an upload that is in progress at shutdown does not crash.
// This verifies that https://crbug.com/792978 is fixed.
TEST_F(NetworkErrorLoggingEndToEndTest, MAYBE_UploadAtShutdown) {
  upload_should_hang_ = true;

  TestDelegate configure_delegate;
  configure_delegate.set_on_complete(base::DoNothing());
  auto configure_request = url_request_context_->CreateRequest(
      GetConfigureURL(), DEFAULT_PRIORITY, &configure_delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  configure_request->set_method("GET");
  configure_request->Start();

  TestDelegate fail_delegate;
  fail_delegate.set_on_complete(base::DoNothing());
  auto fail_request = url_request_context_->CreateRequest(
      GetFailURL(), DEFAULT_PRIORITY, &fail_delegate,
      TRAFFIC_ANNOTATION_FOR_TESTS);
  fail_request->set_method("GET");
  fail_request->Start();

  upload_run_loop_.Run();

  // Let Reporting and NEL shut down with the upload still pending to see if
  // they crash.
}

}  // namespace
}  // namespace net
