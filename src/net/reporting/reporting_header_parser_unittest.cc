// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_header_parser.h"

#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {
namespace {

class ReportingHeaderParserTest : public ReportingTestBase {
 protected:
  void ParseHeader(const GURL& url, const std::string& json) {
    std::unique_ptr<base::Value> value =
        base::JSONReader::Read("[" + json + "]");
    if (value)
      ReportingHeaderParser::ParseHeader(context(), url, std::move(value));
  }

  const GURL kUrl_ = GURL("https://origin/path");
  const url::Origin kOrigin_ = url::Origin::Create(GURL("https://origin/"));
  const GURL kEndpoint_ = GURL("https://endpoint/");
  const std::string kGroup_ = "group";
  const std::string kType_ = "type";
};

// TODO(juliatuttle): Ideally these tests should be expecting that JSON parsing
// (and therefore header parsing) may happen asynchronously, but the entire
// pipeline is also tested by NetworkErrorLoggingEndToEndTest.

TEST_F(ReportingHeaderParserTest, Invalid) {
  static const struct {
    const char* header_value;
    const char* description;
  } kInvalidHeaderTestCases[] = {
      {"{\"max_age\":1, \"endpoints\": [{}]}", "missing url"},
      {"{\"max_age\":1, \"endpoints\": [{\"url\":0}]}", "non-string url"},
      {"{\"max_age\":1, \"endpoints\": [{\"url\":\"http://insecure/\"}]}",
       "insecure url"},

      {"{\"endpoints\": [{\"url\":\"https://endpoint/\"}]}", "missing max_age"},
      {"{\"max_age\":\"\", \"endpoints\": [{\"url\":\"https://endpoint/\"}]}",
       "non-integer max_age"},
      {"{\"max_age\":-1, \"endpoints\": [{\"url\":\"https://endpoint/\"}]}",
       "negative max_age"},
      {"{\"max_age\":1, \"group\":0, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\"}]}",
       "non-string group"},

      // Note that a non-boolean include_subdomains field is *not* invalid, per
      // the spec.

      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"priority\":\"\"}]}",
       "non-integer priority"},

      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"weight\":\"\"}]}",
       "non-integer weight"},
      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"weight\":-1}]}",
       "negative weight"},
      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"weight\":0}]}",
       "zero weight"},

      {"[{\"max_age\":1, \"endpoints\": [{\"url\":\"https://a/\"}]},"
       "{\"max_age\":1, \"endpoints\": [{\"url\":\"https://b/\"}]}]",
       "wrapped in list"}};

  for (size_t i = 0; i < arraysize(kInvalidHeaderTestCases); ++i) {
    auto& test_case = kInvalidHeaderTestCases[i];
    ParseHeader(kUrl_, test_case.header_value);

    std::vector<const ReportingClient*> clients;
    cache()->GetClients(&clients);
    EXPECT_TRUE(clients.empty())
        << "Invalid Report-To header (" << test_case.description << ": \""
        << test_case.header_value << "\") parsed as valid.";
  }
}

TEST_F(ReportingHeaderParserTest, Valid) {
  ParseHeader(kUrl_, "{\"endpoints\": [{\"url\":\"" + kEndpoint_.spec() +
                         "\"}],\"max_age\":86400}");

  const ReportingClient* client =
      FindClientInCache(cache(), kOrigin_, kEndpoint_);
  ASSERT_TRUE(client);
  EXPECT_EQ(kOrigin_, client->origin);
  EXPECT_EQ(kEndpoint_, client->endpoint);
  EXPECT_EQ(ReportingClient::Subdomains::EXCLUDE, client->subdomains);
  EXPECT_EQ(86400, (client->expires - tick_clock()->NowTicks()).InSeconds());
  EXPECT_EQ(ReportingClient::kDefaultPriority, client->priority);
  EXPECT_EQ(ReportingClient::kDefaultWeight, client->weight);
}

TEST_F(ReportingHeaderParserTest, ZeroMaxAge) {
  cache()->SetClient(
      kOrigin_, kEndpoint_, ReportingClient::Subdomains::EXCLUDE, kGroup_,
      tick_clock()->NowTicks() + base::TimeDelta::FromDays(1),
      ReportingClient::kDefaultPriority, ReportingClient::kDefaultWeight);

  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\"}],\"max_age\":0}");

  EXPECT_EQ(nullptr, FindClientInCache(cache(), kOrigin_, kEndpoint_));
}

TEST_F(ReportingHeaderParserTest, Subdomains) {
  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\"}],\"max_age\":86400,"
                         "\"include_subdomains\":true}");

  const ReportingClient* client =
      FindClientInCache(cache(), kOrigin_, kEndpoint_);
  ASSERT_TRUE(client);
  EXPECT_EQ(ReportingClient::Subdomains::INCLUDE, client->subdomains);
}

TEST_F(ReportingHeaderParserTest, PriorityPositive) {
  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\",\"priority\":2}],\"max_age\":86400}");

  const ReportingClient* client =
      FindClientInCache(cache(), kOrigin_, kEndpoint_);
  ASSERT_TRUE(client);
  EXPECT_EQ(2, client->priority);
}

TEST_F(ReportingHeaderParserTest, PriorityNegative) {
  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\",\"priority\":-2}],\"max_age\":86400}");

  const ReportingClient* client =
      FindClientInCache(cache(), kOrigin_, kEndpoint_);
  ASSERT_TRUE(client);
  EXPECT_EQ(-2, client->priority);
}

TEST_F(ReportingHeaderParserTest, Weight) {
  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\",\"weight\":3}],\"max_age\":86400}");

  const ReportingClient* client =
      FindClientInCache(cache(), kOrigin_, kEndpoint_);
  ASSERT_TRUE(client);
  EXPECT_EQ(3, client->weight);
}

TEST_F(ReportingHeaderParserTest, RemoveOld) {
  static const GURL kDifferentEndpoint_ = GURL("https://endpoint2/");

  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\"}],\"max_age\":86400}");

  EXPECT_TRUE(FindClientInCache(cache(), kOrigin_, kEndpoint_));

  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" +
                         kDifferentEndpoint_.spec() +
                         "\"}],\"max_age\":86400}");

  EXPECT_FALSE(FindClientInCache(cache(), kOrigin_, kEndpoint_));
  EXPECT_TRUE(FindClientInCache(cache(), kOrigin_, kDifferentEndpoint_));
}

}  // namespace
}  // namespace net
