// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_service.h"

#include <memory>
#include <string>

#include "base/memory/ptr_util.h"
#include "base/time/tick_clock.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_context.h"
#include "net/reporting/reporting_policy.h"
#include "net/reporting/reporting_report.h"
#include "net/reporting/reporting_service.h"
#include "net/reporting/reporting_test_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

class ReportingServiceTest : public TestWithScopedTaskEnvironment {
 protected:
  const GURL kUrl_ = GURL("https://origin/path");
  const url::Origin kOrigin_ = url::Origin::Create(kUrl_);
  const GURL kEndpoint_ = GURL("https://endpoint/");
  const std::string kUserAgent_ = "Mozilla/1.0";
  const std::string kGroup_ = "group";
  const std::string kType_ = "type";

  ReportingServiceTest()
      : context_(
            new TestReportingContext(&clock_, &tick_clock_, ReportingPolicy())),
        service_(
            ReportingService::CreateForTesting(base::WrapUnique(context_))) {}

  TestReportingContext* context() { return context_; }
  ReportingService* service() { return service_.get(); }

 private:
  base::SimpleTestClock clock_;
  base::SimpleTestTickClock tick_clock_;

  TestReportingContext* context_;
  std::unique_ptr<ReportingService> service_;
};

TEST_F(ReportingServiceTest, QueueReport) {
  service()->QueueReport(kUrl_, kUserAgent_, kGroup_, kType_,
                         std::make_unique<base::DictionaryValue>(), 0);

  std::vector<const ReportingReport*> reports;
  context()->cache()->GetReports(&reports);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(kUrl_, reports[0]->url);
  EXPECT_EQ(kUserAgent_, reports[0]->user_agent);
  EXPECT_EQ(kGroup_, reports[0]->group);
  EXPECT_EQ(kType_, reports[0]->type);
}

TEST_F(ReportingServiceTest, QueueReportSanitizeUrl) {
  // Same as kUrl_ but with username, password, and fragment.
  GURL url = GURL("https://username:password@origin/path#fragment");
  service()->QueueReport(url, kUserAgent_, kGroup_, kType_,
                         std::make_unique<base::DictionaryValue>(), 0);

  std::vector<const ReportingReport*> reports;
  context()->cache()->GetReports(&reports);
  ASSERT_EQ(1u, reports.size());
  EXPECT_EQ(kUrl_, reports[0]->url);
  EXPECT_EQ(kUserAgent_, reports[0]->user_agent);
  EXPECT_EQ(kGroup_, reports[0]->group);
  EXPECT_EQ(kType_, reports[0]->type);
}

TEST_F(ReportingServiceTest, DontQueueReportInvalidUrl) {
  GURL url = GURL("https://");
  service()->QueueReport(url, kUserAgent_, kGroup_, kType_,
                         std::make_unique<base::DictionaryValue>(), 0);

  std::vector<const ReportingReport*> reports;
  context()->cache()->GetReports(&reports);
  ASSERT_EQ(0u, reports.size());
}

TEST_F(ReportingServiceTest, ProcessHeader) {
  service()->ProcessHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" +
                                      kEndpoint_.spec() +
                                      "\"}],"
                                      "\"group\":\"" +
                                      kGroup_ +
                                      "\","
                                      "\"max_age\":86400}");

  EXPECT_EQ(1u, context()->cache()->GetEndpointCount());
}

TEST_F(ReportingServiceTest, ProcessHeader_TooLong) {
  const std::string header_too_long =
      "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
      "\"}],"
      "\"group\":\"" +
      kGroup_ +
      "\","
      "\"max_age\":86400," +
      "\"junk\":\"" + std::string(32 * 1024, 'a') + "\"}";
  service()->ProcessHeader(kUrl_, header_too_long);

  EXPECT_EQ(0u, context()->cache()->GetEndpointCount());
}

TEST_F(ReportingServiceTest, ProcessHeader_TooDeep) {
  const std::string header_too_deep = "{\"endpoints\":[{\"url\":\"" +
                                      kEndpoint_.spec() +
                                      "\"}],"
                                      "\"group\":\"" +
                                      kGroup_ +
                                      "\","
                                      "\"max_age\":86400," +
                                      "\"junk\":[[[[[[[[[[]]]]]]]]]]}";
  service()->ProcessHeader(kUrl_, header_too_deep);

  EXPECT_EQ(0u, context()->cache()->GetEndpointCount());
}

}  // namespace
}  // namespace net
