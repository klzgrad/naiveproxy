// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <vector>

#include "base/bind.h"
#include "base/callback.h"
#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "base/test/simple_test_clock.h"
#include "base/test/values_test_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/ip_address.h"
#include "net/base/net_errors.h"
#include "net/network_error_logging/network_error_logging_delegate.h"
#include "net/network_error_logging/network_error_logging_service.h"
#include "net/reporting/reporting_policy.h"
#include "net/reporting/reporting_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {
namespace {

class TestReportingService : public ReportingService {
 public:
  struct Report {
    Report() = default;

    Report(Report&& other)
        : url(other.url),
          user_agent(other.user_agent),
          group(other.group),
          type(other.type),
          body(std::move(other.body)),
          depth(other.depth) {}

    Report(const GURL& url,
           const std::string& user_agent,
           const std::string& group,
           const std::string& type,
           std::unique_ptr<const base::Value> body,
           int depth)
        : url(url),
          user_agent(user_agent),
          group(group),
          type(type),
          body(std::move(body)),
          depth(depth) {}

    ~Report() = default;

    GURL url;
    std::string user_agent;
    std::string group;
    std::string type;
    std::unique_ptr<const base::Value> body;
    int depth;

   private:
    DISALLOW_COPY(Report);
  };

  TestReportingService() = default;

  const std::vector<Report>& reports() const { return reports_; }

  // ReportingService implementation:

  ~TestReportingService() override = default;

  void QueueReport(const GURL& url,
                   const std::string& user_agent,
                   const std::string& group,
                   const std::string& type,
                   std::unique_ptr<const base::Value> body,
                   int depth) override {
    reports_.push_back(
        Report(url, user_agent, group, type, std::move(body), depth));
  }

  void ProcessHeader(const GURL& url,
                     const std::string& header_value) override {
    NOTREACHED();
  }

  void RemoveBrowsingData(int data_type_mask,
                          const base::RepeatingCallback<bool(const GURL&)>&
                              origin_filter) override {
    NOTREACHED();
  }

  void RemoveAllBrowsingData(int data_type_mask) override { NOTREACHED(); }

  void OnShutdown() override {}

  const ReportingPolicy& GetPolicy() const override {
    NOTREACHED();
    return dummy_policy_;
  }

  ReportingContext* GetContextForTesting() const override {
    NOTREACHED();
    return nullptr;
  }

 private:
  std::vector<Report> reports_;
  ReportingPolicy dummy_policy_;

  DISALLOW_COPY_AND_ASSIGN(TestReportingService);
};

class NetworkErrorLoggingServiceTest : public ::testing::Test {
 protected:
  NetworkErrorLoggingServiceTest() {
    service_ = NetworkErrorLoggingService::Create(
        NetworkErrorLoggingDelegate::Create());
    CreateReportingService();
  }

  void CreateReportingService() {
    DCHECK(!reporting_service_);

    reporting_service_ = std::make_unique<TestReportingService>();
    service_->SetReportingService(reporting_service_.get());
  }

  void DestroyReportingService() {
    DCHECK(reporting_service_);

    service_->SetReportingService(nullptr);
    reporting_service_.reset();
  }

  NetworkErrorLoggingService::RequestDetails MakeRequestDetails(
      GURL url,
      Error error_type,
      std::string method = "GET",
      int status_code = 0,
      IPAddress server_ip = IPAddress()) {
    NetworkErrorLoggingService::RequestDetails details;

    details.uri = url;
    details.referrer = kReferrer_;
    details.user_agent = kUserAgent_;
    details.server_ip = server_ip.IsValid() ? server_ip : kServerIP_;
    details.method = std::move(method);
    details.status_code = status_code;
    details.elapsed_time = base::TimeDelta::FromSeconds(1);
    details.type = error_type;
    details.reporting_upload_depth = 0;

    return details;
  }

  NetworkErrorLoggingService::SignedExchangeReportDetails
  MakeSignedExchangeReportDetails(bool success,
                                  const std::string& type,
                                  const GURL& outer_url,
                                  const GURL& inner_url,
                                  const GURL& cert_url,
                                  const IPAddress& server_ip_address) {
    NetworkErrorLoggingService::SignedExchangeReportDetails details;
    details.success = success;
    details.type = type;
    details.outer_url = outer_url;
    details.inner_url = inner_url;
    details.cert_url = cert_url;
    details.referrer = kReferrer_.spec();
    details.server_ip_address = server_ip_address;
    details.protocol = "http/1.1";
    details.method = "GET";
    details.status_code = 200;
    details.elapsed_time = base::TimeDelta::FromMilliseconds(1234);
    details.user_agent = kUserAgent_;
    return details;
  }
  NetworkErrorLoggingService* service() { return service_.get(); }
  const std::vector<TestReportingService::Report>& reports() {
    return reporting_service_->reports();
  }

  const url::Origin MakeOrigin(size_t index) {
    GURL url(base::StringPrintf("https://example%zd.com/", index));
    return url::Origin::Create(url);
  }

  // Returns whether the NetworkErrorLoggingService has a policy corresponding
  // to |origin|. Returns true if so, even if the policy is expired.
  bool HasPolicyForOrigin(const url::Origin& origin) {
    std::set<url::Origin> all_policy_origins =
        service_->GetPolicyOriginsForTesting();
    return all_policy_origins.find(origin) != all_policy_origins.end();
  }

  size_t PolicyCount() { return service_->GetPolicyOriginsForTesting().size(); }

  const GURL kUrl_ = GURL("https://example.com/path");
  const GURL kUrlDifferentPort_ = GURL("https://example.com:4433/path");
  const GURL kUrlSubdomain_ = GURL("https://subdomain.example.com/path");
  const GURL kUrlDifferentHost_ = GURL("https://example2.com/path");

  const GURL kInnerUrl_ = GURL("https://example.net/path");
  const GURL kCertUrl_ = GURL("https://example.com/cert_path");

  const IPAddress kServerIP_ = IPAddress(192, 168, 0, 1);
  const IPAddress kOtherServerIP_ = IPAddress(192, 168, 0, 2);
  const url::Origin kOrigin_ = url::Origin::Create(kUrl_);
  const url::Origin kOriginDifferentPort_ =
      url::Origin::Create(kUrlDifferentPort_);
  const url::Origin kOriginSubdomain_ = url::Origin::Create(kUrlSubdomain_);
  const url::Origin kOriginDifferentHost_ =
      url::Origin::Create(kUrlDifferentHost_);

  const std::string kHeader_ = "{\"report_to\":\"group\",\"max_age\":86400}";
  const std::string kHeaderSuccessFraction0_ =
      "{\"report_to\":\"group\",\"max_age\":86400,\"success_fraction\":0.0}";
  const std::string kHeaderSuccessFraction1_ =
      "{\"report_to\":\"group\",\"max_age\":86400,\"success_fraction\":1.0}";
  const std::string kHeaderIncludeSubdomains_ =
      "{\"report_to\":\"group\",\"max_age\":86400,\"include_subdomains\":true}";
  const std::string kHeaderMaxAge0_ = "{\"max_age\":0}";
  const std::string kHeaderTooLong_ =
      "{\"report_to\":\"group\",\"max_age\":86400,\"junk\":\"" +
      std::string(32 * 1024, 'a') + "\"}";
  const std::string kHeaderTooDeep_ =
      "{\"report_to\":\"group\",\"max_age\":86400,\"junk\":[[[[[[[[[[]]]]]]]]]]"
      "}";

  const std::string kUserAgent_ = "Mozilla/1.0";
  const std::string kGroup_ = "group";

  const std::string kType_ = NetworkErrorLoggingService::kReportType;

  const GURL kReferrer_ = GURL("https://referrer.com/");

 private:
  std::unique_ptr<NetworkErrorLoggingService> service_;
  std::unique_ptr<TestReportingService> reporting_service_;
};

void ExpectDictDoubleValue(double expected_value,
                           const base::DictionaryValue& value,
                           const std::string& key) {
  double double_value = 0.0;
  EXPECT_TRUE(value.GetDouble(key, &double_value)) << key;
  EXPECT_DOUBLE_EQ(expected_value, double_value) << key;
}

TEST_F(NetworkErrorLoggingServiceTest, CreateService) {
  // Service is created by default in the test fixture..
  EXPECT_TRUE(service());
}

TEST_F(NetworkErrorLoggingServiceTest, NoReportingService) {
  DestroyReportingService();

  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));
}

TEST_F(NetworkErrorLoggingServiceTest, NoPolicyForOrigin) {
  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, JsonTooLong) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderTooLong_);

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, JsonTooDeep) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderTooDeep_);

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, SuccessReportQueued) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);

  service()->OnRequest(MakeRequestDetails(kUrl_, OK));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kUserAgent_, reports()[0].user_agent);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  // TODO(juliatuttle): Extract these constants.
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(1000, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue("application", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("ok", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, FailureReportQueued) {
  static const std::string kHeaderFailureFraction1 =
      "{\"report_to\":\"group\",\"max_age\":86400,\"failure_fraction\":1.0}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderFailureFraction1);

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kUserAgent_, reports()[0].user_agent);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  // TODO(juliatuttle): Extract these constants.
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(1000, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue("connection", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("tcp.refused", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, UnknownFailureReportQueued) {
  static const std::string kHeaderFailureFraction1 =
      "{\"report_to\":\"group\",\"max_age\":86400,\"failure_fraction\":1.0}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderFailureFraction1);

  // This error code happens to not be mapped to a NEL report `type` field
  // value.
  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_FILE_NO_SPACE));

  ASSERT_EQ(1u, reports().size());
  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue("application", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("unknown", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, UnknownCertFailureReportQueued) {
  static const std::string kHeaderFailureFraction1 =
      "{\"report_to\":\"group\",\"max_age\":86400,\"failure_fraction\":1.0}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderFailureFraction1);

  // This error code happens to not be mapped to a NEL report `type` field
  // value.  Because it's a certificate error, we'll set the `phase` to be
  // `connection`.
  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CERT_NON_UNIQUE_NAME));

  ASSERT_EQ(1u, reports().size());
  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue("connection", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("unknown", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, HttpErrorReportQueued) {
  static const std::string kHeaderFailureFraction1 =
      "{\"report_to\":\"group\",\"max_age\":86400,\"failure_fraction\":1.0}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderFailureFraction1);

  service()->OnRequest(MakeRequestDetails(kUrl_, OK, "GET", 504));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kUserAgent_, reports()[0].user_agent);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  // TODO(juliatuttle): Extract these constants.
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(504, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(1000, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue("application", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("http.error", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, SuccessReportDowngraded) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);

  service()->OnRequest(
      MakeRequestDetails(kUrl_, OK, "GET", 200, kOtherServerIP_));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kOtherServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue("dns", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("dns.address_changed", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, FailureReportDowngraded) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED, "GET",
                                          200, kOtherServerIP_));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kOtherServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue("dns", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("dns.address_changed", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, HttpErrorReportDowngraded) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);

  service()->OnRequest(
      MakeRequestDetails(kUrl_, OK, "GET", 504, kOtherServerIP_));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kOtherServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue("dns", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("dns.address_changed", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, DNSFailureReportNotDowngraded) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_NAME_NOT_RESOLVED, "GET",
                                          0, kOtherServerIP_));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kOtherServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(0, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(1000, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue("dns", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("dns.name_not_resolved", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, SuccessPOSTReportQueued) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);

  service()->OnRequest(MakeRequestDetails(kUrl_, OK, "POST"));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("POST", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictStringValue("application", *body,
                              NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("ok", *body,
                              NetworkErrorLoggingService::kTypeKey);
}

TEST_F(NetworkErrorLoggingServiceTest, MaxAge0) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  EXPECT_EQ(1u, PolicyCount());

  // Max_age of 0 removes the policy.
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderMaxAge0_);
  EXPECT_EQ(0u, PolicyCount());

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, SuccessFraction0) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction0_);

  // Each network error has a 0% chance of being reported.  Fire off several and
  // verify that no reports are produced.
  constexpr size_t kReportCount = 100;
  for (size_t i = 0; i < kReportCount; ++i)
    service()->OnRequest(MakeRequestDetails(kUrl_, OK));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, SuccessFractionHalf) {
  // Include a different value for failure_fraction to ensure that we copy the
  // right value into sampling_fraction.
  static const std::string kHeaderSuccessFractionHalf =
      "{\"report_to\":\"group\",\"max_age\":86400,\"success_fraction\":0.5,"
      "\"failure_fraction\":0.25}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFractionHalf);

  // Each network error has a 50% chance of being reported.  Fire off several
  // and verify that some requests were reported and some weren't.  (We can't
  // verify exact counts because each decision is made randomly.)
  constexpr size_t kReportCount = 100;
  for (size_t i = 0; i < kReportCount; ++i)
    service()->OnRequest(MakeRequestDetails(kUrl_, OK));

  // If our random selection logic is correct, there is a 2^-100 chance that
  // every single report above was skipped.  If this check fails, it's much more
  // likely that our code is wrong.
  EXPECT_FALSE(reports().empty());

  // There's also a 2^-100 chance that every single report was logged.  Same as
  // above, that's much more likely to be a code error.
  EXPECT_GT(kReportCount, reports().size());

  for (const auto& report : reports()) {
    const base::DictionaryValue* body;
    ASSERT_TRUE(report.body->GetAsDictionary(&body));
    // Our header includes a different value for failure_fraction, so that this
    // check verifies that we copy the correct fraction into sampling_fraction.
    ExpectDictDoubleValue(0.5, *body,
                          NetworkErrorLoggingService::kSamplingFractionKey);
  }
}

TEST_F(NetworkErrorLoggingServiceTest, FailureFraction0) {
  static const std::string kHeaderFailureFraction0 =
      "{\"report_to\":\"group\",\"max_age\":86400,\"failure_fraction\":0.0}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderFailureFraction0);

  // Each network error has a 0% chance of being reported.  Fire off several and
  // verify that no reports are produced.
  constexpr size_t kReportCount = 100;
  for (size_t i = 0; i < kReportCount; ++i)
    service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, FailureFractionHalf) {
  // Include a different value for success_fraction to ensure that we copy the
  // right value into sampling_fraction.
  static const std::string kHeaderFailureFractionHalf =
      "{\"report_to\":\"group\",\"max_age\":86400,\"failure_fraction\":0.5,"
      "\"success_fraction\":0.25}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderFailureFractionHalf);

  // Each network error has a 50% chance of being reported.  Fire off several
  // and verify that some requests were reported and some weren't.  (We can't
  // verify exact counts because each decision is made randomly.)
  constexpr size_t kReportCount = 100;
  for (size_t i = 0; i < kReportCount; ++i)
    service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  // If our random selection logic is correct, there is a 2^-100 chance that
  // every single report above was skipped.  If this check fails, it's much more
  // likely that our code is wrong.
  EXPECT_FALSE(reports().empty());

  // There's also a 2^-100 chance that every single report was logged.  Same as
  // above, that's much more likely to be a code error.
  EXPECT_GT(kReportCount, reports().size());

  for (const auto& report : reports()) {
    const base::DictionaryValue* body;
    ASSERT_TRUE(report.body->GetAsDictionary(&body));
    ExpectDictDoubleValue(0.5, *body,
                          NetworkErrorLoggingService::kSamplingFractionKey);
  }
}

TEST_F(NetworkErrorLoggingServiceTest,
       ExcludeSubdomainsDoesntMatchDifferentPort) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);

  service()->OnRequest(
      MakeRequestDetails(kUrlDifferentPort_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, ExcludeSubdomainsDoesntMatchSubdomain) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);

  service()->OnRequest(
      MakeRequestDetails(kUrlSubdomain_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, IncludeSubdomainsMatchesDifferentPort) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderIncludeSubdomains_);

  service()->OnRequest(
      MakeRequestDetails(kUrlDifferentPort_, ERR_NAME_NOT_RESOLVED));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrlDifferentPort_, reports()[0].url);
}

TEST_F(NetworkErrorLoggingServiceTest, IncludeSubdomainsMatchesSubdomain) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderIncludeSubdomains_);

  service()->OnRequest(
      MakeRequestDetails(kUrlSubdomain_, ERR_NAME_NOT_RESOLVED));

  ASSERT_EQ(1u, reports().size());
}

TEST_F(NetworkErrorLoggingServiceTest,
       IncludeSubdomainsDoesntMatchSuperdomain) {
  service()->OnHeader(kOriginSubdomain_, kServerIP_, kHeaderIncludeSubdomains_);

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_NAME_NOT_RESOLVED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest,
       IncludeSubdomainsDoesntReportConnectionError) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderIncludeSubdomains_);

  service()->OnRequest(
      MakeRequestDetails(kUrlSubdomain_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest,
       IncludeSubdomainsDoesntReportApplicationError) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderIncludeSubdomains_);

  service()->OnRequest(
      MakeRequestDetails(kUrlSubdomain_, ERR_INVALID_HTTP_RESPONSE));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, IncludeSubdomainsDoesntReportSuccess) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderIncludeSubdomains_);

  service()->OnRequest(MakeRequestDetails(kUrlSubdomain_, OK));

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest,
       IncludeSubdomainsReportsSameOriginSuccess) {
  static const std::string kHeaderIncludeSubdomainsSuccess1 =
      "{\"report_to\":\"group\",\"max_age\":86400,"
      "\"include_subdomains\":true,\"success_fraction\":1.0}";
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderIncludeSubdomainsSuccess1);

  service()->OnRequest(MakeRequestDetails(kUrl_, OK));

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
}

TEST_F(NetworkErrorLoggingServiceTest, RemoveAllBrowsingData) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  EXPECT_EQ(1u, PolicyCount());
  EXPECT_TRUE(HasPolicyForOrigin(kOrigin_));

  service()->RemoveAllBrowsingData();

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  EXPECT_EQ(0u, PolicyCount());
  EXPECT_FALSE(HasPolicyForOrigin(kOrigin_));
  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, RemoveSomeBrowsingData) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  service()->OnHeader(kOriginDifferentHost_, kServerIP_, kHeader_);
  EXPECT_EQ(2u, PolicyCount());

  // Remove policy for kOrigin_ but not kOriginDifferentHost_
  service()->RemoveBrowsingData(
      base::BindRepeating([](const GURL& origin) -> bool {
        return origin.host() == "example.com";
      }));
  EXPECT_EQ(1u, PolicyCount());
  EXPECT_TRUE(HasPolicyForOrigin(kOriginDifferentHost_));
  EXPECT_FALSE(HasPolicyForOrigin(kOrigin_));

  service()->OnRequest(MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED));

  EXPECT_TRUE(reports().empty());

  service()->OnRequest(
      MakeRequestDetails(kUrlDifferentHost_, ERR_CONNECTION_REFUSED));

  ASSERT_EQ(1u, reports().size());
}

TEST_F(NetworkErrorLoggingServiceTest, Nested) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);

  NetworkErrorLoggingService::RequestDetails details =
      MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED);
  details.reporting_upload_depth =
      NetworkErrorLoggingService::kMaxNestedReportDepth;
  service()->OnRequest(details);

  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(NetworkErrorLoggingService::kMaxNestedReportDepth,
            reports()[0].depth);
}

TEST_F(NetworkErrorLoggingServiceTest, NestedTooDeep) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);

  NetworkErrorLoggingService::RequestDetails details =
      MakeRequestDetails(kUrl_, ERR_CONNECTION_REFUSED);
  details.reporting_upload_depth =
      NetworkErrorLoggingService::kMaxNestedReportDepth + 1;
  service()->OnRequest(details);

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, StatusAsValue) {
  // The expiration times will be bogus, but we need a reproducible value for
  // this test.
  base::SimpleTestClock clock;
  service()->SetClockForTesting(&clock);
  // The clock is initialized to the "zero" or origin point of the Time class.
  // This sets the clock's Time to the equivalent of the "zero" or origin point
  // of the TimeTicks class, so that the serialized value produced by
  // NetLog::TimeToString is consistent across restarts.
  base::TimeDelta delta_from_origin =
      base::Time::UnixEpoch().since_origin() -
      base::TimeTicks::UnixEpoch().since_origin();
  clock.Advance(delta_from_origin);

  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);
  service()->OnHeader(kOriginDifferentHost_, kServerIP_, kHeader_);
  service()->OnHeader(kOriginSubdomain_, kServerIP_, kHeaderIncludeSubdomains_);
  const std::string kHeaderWrongTypes =
      ("{\"report_to\":\"group\","
       "\"max_age\":86400,"
       // We'll ignore each of these fields because they're the wrong type.
       // We'll use a default value instead.
       "\"include_subdomains\":\"true\","
       "\"success_fraction\": \"1.0\","
       "\"failure_fraction\": \"0.0\"}");
  service()->OnHeader(
      url::Origin::Create(GURL("https://invalid-types.example.com")),
      kServerIP_, kHeaderWrongTypes);

  base::Value actual = service()->StatusAsValue();
  std::unique_ptr<base::Value> expected =
      base::test::ParseJsonDeprecated(R"json(
      {
        "originPolicies": [
          {
            "origin": "https://example.com",
            "includeSubdomains": false,
            "expires": "86400000",
            "reportTo": "group",
            "successFraction": 1.0,
            "failureFraction": 1.0,
          },
          {
            "origin": "https://example2.com",
            "includeSubdomains": false,
            "expires": "86400000",
            "reportTo": "group",
            "successFraction": 0.0,
            "failureFraction": 1.0,
          },
          {
            "origin": "https://invalid-types.example.com",
            "includeSubdomains": false,
            "expires": "86400000",
            "reportTo": "group",
            "successFraction": 0.0,
            "failureFraction": 1.0,
          },
          {
            "origin": "https://subdomain.example.com",
            "includeSubdomains": true,
            "expires": "86400000",
            "reportTo": "group",
            "successFraction": 0.0,
            "failureFraction": 1.0,
          },
        ]
      }
      )json");
  EXPECT_EQ(*expected, actual);
}

TEST_F(NetworkErrorLoggingServiceTest, NoReportingService_SignedExchange) {
  DestroyReportingService();

  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  service()->QueueSignedExchangeReport(MakeSignedExchangeReportDetails(
      false, "sxg.failed", kUrl_, kInnerUrl_, kCertUrl_, kServerIP_));
}

TEST_F(NetworkErrorLoggingServiceTest, NoPolicyForOrigin_SignedExchange) {
  service()->QueueSignedExchangeReport(MakeSignedExchangeReportDetails(
      false, "sxg.failed", kUrl_, kInnerUrl_, kCertUrl_, kServerIP_));
  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, SuccessFraction0_SignedExchange) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction0_);

  // Each network error has a 0% chance of being reported.  Fire off several and
  // verify that no reports are produced.
  constexpr size_t kReportCount = 100;
  for (size_t i = 0; i < kReportCount; ++i) {
    service()->QueueSignedExchangeReport(MakeSignedExchangeReportDetails(
        true, "ok", kUrl_, kInnerUrl_, kCertUrl_, kServerIP_));
  }

  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, SuccessReportQueued_SignedExchange) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderSuccessFraction1_);
  service()->QueueSignedExchangeReport(MakeSignedExchangeReportDetails(
      true, "ok", kUrl_, kInnerUrl_, kCertUrl_, kServerIP_));
  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kUserAgent_, reports()[0].user_agent);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("http/1.1", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(200, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(1234, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue(
      NetworkErrorLoggingService::kSignedExchangePhaseValue, *body,
      NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("ok", *body,
                              NetworkErrorLoggingService::kTypeKey);

  const base::DictionaryValue* sxg_body;
  ASSERT_TRUE(body->FindKey(NetworkErrorLoggingService::kSignedExchangeBodyKey)
                  ->GetAsDictionary(&sxg_body));

  base::ExpectDictStringValue(kUrl_.spec(), *sxg_body,
                              NetworkErrorLoggingService::kOuterUrlKey);
  base::ExpectDictStringValue(kInnerUrl_.spec(), *sxg_body,
                              NetworkErrorLoggingService::kInnerUrlKey);
  base::ExpectStringValue(
      kCertUrl_.spec(),
      sxg_body->FindKey(NetworkErrorLoggingService::kCertUrlKey)->GetList()[0]);
}

TEST_F(NetworkErrorLoggingServiceTest, FailureReportQueued_SignedExchange) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  service()->QueueSignedExchangeReport(MakeSignedExchangeReportDetails(
      false, "sxg.failed", kUrl_, kInnerUrl_, kCertUrl_, kServerIP_));
  ASSERT_EQ(1u, reports().size());
  EXPECT_EQ(kUrl_, reports()[0].url);
  EXPECT_EQ(kUserAgent_, reports()[0].user_agent);
  EXPECT_EQ(kGroup_, reports()[0].group);
  EXPECT_EQ(kType_, reports()[0].type);
  EXPECT_EQ(0, reports()[0].depth);

  const base::DictionaryValue* body;
  ASSERT_TRUE(reports()[0].body->GetAsDictionary(&body));
  base::ExpectDictStringValue(kReferrer_.spec(), *body,
                              NetworkErrorLoggingService::kReferrerKey);
  ExpectDictDoubleValue(1.0, *body,
                        NetworkErrorLoggingService::kSamplingFractionKey);
  base::ExpectDictStringValue(kServerIP_.ToString(), *body,
                              NetworkErrorLoggingService::kServerIpKey);
  base::ExpectDictStringValue("http/1.1", *body,
                              NetworkErrorLoggingService::kProtocolKey);
  base::ExpectDictStringValue("GET", *body,
                              NetworkErrorLoggingService::kMethodKey);
  base::ExpectDictIntegerValue(200, *body,
                               NetworkErrorLoggingService::kStatusCodeKey);
  base::ExpectDictIntegerValue(1234, *body,
                               NetworkErrorLoggingService::kElapsedTimeKey);
  base::ExpectDictStringValue(
      NetworkErrorLoggingService::kSignedExchangePhaseValue, *body,
      NetworkErrorLoggingService::kPhaseKey);
  base::ExpectDictStringValue("sxg.failed", *body,
                              NetworkErrorLoggingService::kTypeKey);

  const base::DictionaryValue* sxg_body;
  ASSERT_TRUE(body->FindKey(NetworkErrorLoggingService::kSignedExchangeBodyKey)
                  ->GetAsDictionary(&sxg_body));

  base::ExpectDictStringValue(kUrl_.spec(), *sxg_body,
                              NetworkErrorLoggingService::kOuterUrlKey);
  base::ExpectDictStringValue(kInnerUrl_.spec(), *sxg_body,
                              NetworkErrorLoggingService::kInnerUrlKey);
  base::ExpectStringValue(
      kCertUrl_.spec(),
      sxg_body->FindKey(NetworkErrorLoggingService::kCertUrlKey)->GetList()[0]);
}

TEST_F(NetworkErrorLoggingServiceTest, MismatchingSubdomain_SignedExchange) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeaderIncludeSubdomains_);
  service()->QueueSignedExchangeReport(MakeSignedExchangeReportDetails(
      false, "sxg.failed", kUrlSubdomain_, kInnerUrl_, kCertUrl_, kServerIP_));
  EXPECT_TRUE(reports().empty());
}

TEST_F(NetworkErrorLoggingServiceTest, MismatchingIPAddress_SignedExchange) {
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  service()->QueueSignedExchangeReport(MakeSignedExchangeReportDetails(
      false, "sxg.failed", kUrl_, kInnerUrl_, kCertUrl_, kOtherServerIP_));
  EXPECT_TRUE(reports().empty());
}

// When the max number of policies is exceeded, first try to remove expired
// policies before evicting the least recently used unexpired policy.
TEST_F(NetworkErrorLoggingServiceTest, EvictAllExpiredPoliciesFirst) {
  base::SimpleTestClock clock;
  service()->SetClockForTesting(&clock);

  // Add 100 policies then make them expired.
  for (size_t i = 0; i < 100; ++i) {
    service()->OnHeader(MakeOrigin(i), kServerIP_, kHeader_);
  }
  EXPECT_EQ(100u, PolicyCount());
  clock.Advance(base::TimeDelta::FromSeconds(86401));  // max_age is 86400 sec
  // Expired policies are allowed to linger before hitting the policy limit.
  EXPECT_EQ(100u, PolicyCount());

  // Reach the max policy limit.
  for (size_t i = 100; i < NetworkErrorLoggingService::kMaxPolicies; ++i) {
    service()->OnHeader(MakeOrigin(i), kServerIP_, kHeader_);
  }
  EXPECT_EQ(NetworkErrorLoggingService::kMaxPolicies, PolicyCount());

  // Add one more policy to trigger eviction of only the expired policies.
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  EXPECT_EQ(NetworkErrorLoggingService::kMaxPolicies - 100 + 1, PolicyCount());
}

TEST_F(NetworkErrorLoggingServiceTest, EvictLeastRecentlyUsedPolicy) {
  base::SimpleTestClock clock;
  service()->SetClockForTesting(&clock);

  // A policy's |last_used| is updated when it is added
  for (size_t i = 0; i < NetworkErrorLoggingService::kMaxPolicies; ++i) {
    service()->OnHeader(MakeOrigin(i), kServerIP_, kHeader_);
    clock.Advance(base::TimeDelta::FromSeconds(1));
  }

  EXPECT_EQ(PolicyCount(), NetworkErrorLoggingService::kMaxPolicies);

  // Set another policy which triggers eviction. None of the policies have
  // expired, so the least recently used (i.e. least recently added) policy
  // should be evicted.
  service()->OnHeader(kOrigin_, kServerIP_, kHeader_);
  clock.Advance(base::TimeDelta::FromSeconds(1));
  EXPECT_EQ(PolicyCount(), NetworkErrorLoggingService::kMaxPolicies);

  EXPECT_FALSE(HasPolicyForOrigin(MakeOrigin(0)));  // evicted
  std::set<url::Origin> all_policy_origins =
      service()->GetPolicyOriginsForTesting();
  for (size_t i = 1; i < NetworkErrorLoggingService::kMaxPolicies; ++i) {
    // Avoid n calls to HasPolicyForOrigin(), which would be O(n^2).
    EXPECT_EQ(1u, all_policy_origins.count(MakeOrigin(i)));
  }
  EXPECT_TRUE(HasPolicyForOrigin(kOrigin_));

  // Now use the policies in reverse order starting with kOrigin_, then add
  // another policy to trigger eviction, to check that the stalest policy is
  // identified correctly.
  service()->OnRequest(
      MakeRequestDetails(kOrigin_.GetURL(), ERR_CONNECTION_REFUSED));
  clock.Advance(base::TimeDelta::FromSeconds(1));
  for (size_t i = NetworkErrorLoggingService::kMaxPolicies - 1; i >= 1; --i) {
    service()->OnRequest(
        MakeRequestDetails(MakeOrigin(i).GetURL(), ERR_CONNECTION_REFUSED));
    clock.Advance(base::TimeDelta::FromSeconds(1));
  }
  service()->OnHeader(kOriginSubdomain_, kServerIP_, kHeader_);
  EXPECT_EQ(PolicyCount(), NetworkErrorLoggingService::kMaxPolicies);

  EXPECT_FALSE(HasPolicyForOrigin(kOrigin_));  // evicted
  all_policy_origins = service()->GetPolicyOriginsForTesting();
  for (size_t i = NetworkErrorLoggingService::kMaxPolicies - 1; i >= 1; --i) {
    // Avoid n calls to HasPolicyForOrigin(), which would be O(n^2).
    EXPECT_EQ(1u, all_policy_origins.count(MakeOrigin(i)));
  }
  EXPECT_TRUE(HasPolicyForOrigin(kOriginSubdomain_));  // most recently added

  // Note: This test advances the clock by ~2000 seconds, which is below the
  // specified max_age of 86400 seconds, so none of the policies expire during
  // this test.
}

}  // namespace
}  // namespace net
