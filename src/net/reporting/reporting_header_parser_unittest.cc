// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_header_parser.h"

#include <sstream>
#include <string>
#include <vector>

#include "base/json/json_reader.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_endpoint.h"
#include "net/reporting/reporting_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {
namespace {

class ReportingHeaderParserTest : public ReportingTestBase {
 protected:
  ReportingHeaderParserTest() : ReportingTestBase() {
    ReportingPolicy policy;
    policy.max_endpoints_per_origin = 10;
    policy.max_endpoint_count = 20;
    UsePolicy(policy);
  }

  ~ReportingHeaderParserTest() override = default;

  ReportingEndpointGroup MakeEndpointGroup(
      std::string name,
      std::vector<ReportingEndpoint::EndpointInfo> endpoints,
      OriginSubdomains include_subdomains = OriginSubdomains::DEFAULT,
      base::TimeDelta ttl = base::TimeDelta::FromDays(1)) {
    ReportingEndpointGroup group;
    group.name = std::move(name);
    group.include_subdomains = include_subdomains;
    group.ttl = ttl;
    group.endpoints = std::move(endpoints);
    return group;
  }

  // Constructs a string which would represent a single group in a Report-To
  // header. If |group_name| is an empty string, the group name will be omitted
  // (and thus default to "default" when parsed). Setting |omit_defaults| omits
  // the priority, weight, and include_subdomains fields if they are default,
  // otherwise they are spelled out fully.
  std::string ConstructHeaderGroupString(const ReportingEndpointGroup& group,
                                         bool omit_defaults = true) {
    std::ostringstream s;
    s << "{ ";

    if (!group.name.empty()) {
      s << "\"group\": \"" << group.name << "\", ";
    }

    s << "\"max_age\": " << group.ttl.InSeconds() << ", ";

    if (group.include_subdomains != OriginSubdomains::DEFAULT) {
      s << "\"include_subdomains\": true, ";
    } else if (!omit_defaults) {
      s << "\"include_subdomains\": false, ";
    }

    s << "\"endpoints\": [";
    for (const ReportingEndpoint::EndpointInfo& endpoint_info :
         group.endpoints) {
      s << "{ ";
      s << "\"url\": \"" << endpoint_info.url.spec() << "\"";

      if (!omit_defaults ||
          endpoint_info.priority !=
              ReportingEndpoint::EndpointInfo::kDefaultPriority) {
        s << ", \"priority\": " << endpoint_info.priority;
      }

      if (!omit_defaults ||
          endpoint_info.weight !=
              ReportingEndpoint::EndpointInfo::kDefaultWeight) {
        s << ", \"weight\": " << endpoint_info.weight;
      }

      s << " }, ";
    }
    if (!group.endpoints.empty())
      s.seekp(-2, s.cur);  // Overwrite trailing comma and space.
    s << "]";

    s << " }";

    return s.str();
  }

  void ParseHeader(const GURL& url, const std::string& json) {
    std::unique_ptr<base::Value> value =
        base::JSONReader::ReadDeprecated("[" + json + "]");
    if (value)
      ReportingHeaderParser::ParseHeader(context(), url, std::move(value));
  }

  const GURL kUrl_ = GURL("https://origin.test/path");
  const url::Origin kOrigin_ =
      url::Origin::Create(GURL("https://origin.test/"));
  const GURL kUrl2_ = GURL("https://origin2.test/path");
  const url::Origin kOrigin2_ =
      url::Origin::Create(GURL("https://origin2.test/"));
  const GURL kEndpoint_ = GURL("https://endpoint.test/");
  const GURL kEndpoint2_ = GURL("https://endpoint2.test/");
  const std::string kGroup_ = "group";
  const std::string kGroup2_ = "group2";
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

      // Priority should be a nonnegative integer.
      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"priority\":\"\"}]}",
       "non-integer priority"},
      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"priority\":-1}]}",
       "negative priority"},

      // Weight should be a non-negative integer.
      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"weight\":\"\"}]}",
       "non-integer weight"},
      {"{\"max_age\":1, "
       "\"endpoints\": [{\"url\":\"https://endpoint/\",\"weight\":-1}]}",
       "negative weight"},

      {"[{\"max_age\":1, \"endpoints\": [{\"url\":\"https://a/\"}]},"
       "{\"max_age\":1, \"endpoints\": [{\"url\":\"https://b/\"}]}]",
       "wrapped in list"}};

  for (size_t i = 0; i < base::size(kInvalidHeaderTestCases); ++i) {
    auto& test_case = kInvalidHeaderTestCases[i];
    ParseHeader(kUrl_, test_case.header_value);

    EXPECT_EQ(0u, cache()->GetEndpointCount())
        << "Invalid Report-To header (" << test_case.description << ": \""
        << test_case.header_value << "\") parsed as valid.";
  }
}

TEST_F(ReportingHeaderParserTest, Basic) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{kEndpoint_}};

  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints));

  ParseHeader(kUrl_, header);
  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(1u, cache()->GetEndpointCount());
  ReportingEndpoint endpoint =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_);
  ASSERT_TRUE(endpoint);
  EXPECT_EQ(kOrigin_, endpoint.group_key.origin);
  EXPECT_EQ(kGroup_, endpoint.group_key.group_name);
  EXPECT_EQ(kEndpoint_, endpoint.info.url);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint.info.weight);
}

TEST_F(ReportingHeaderParserTest, OmittedGroupName) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{kEndpoint_}};
  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(std::string(), endpoints));

  ParseHeader(kUrl_, header);
  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin_, "default",
                                         OriginSubdomains::DEFAULT));
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(1u, cache()->GetEndpointCount());
  ReportingEndpoint endpoint =
      FindEndpointInCache(kOrigin_, "default", kEndpoint_);
  ASSERT_TRUE(endpoint);
  EXPECT_EQ(kOrigin_, endpoint.group_key.origin);
  EXPECT_EQ("default", endpoint.group_key.group_name);
  EXPECT_EQ(kEndpoint_, endpoint.info.url);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint.info.weight);
}

TEST_F(ReportingHeaderParserTest, IncludeSubdomainsTrue) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{kEndpoint_}};

  std::string header = ConstructHeaderGroupString(
      MakeEndpointGroup(kGroup_, endpoints, OriginSubdomains::INCLUDE));
  ParseHeader(kUrl_, header);

  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::INCLUDE));
  EXPECT_EQ(1u, cache()->GetEndpointCount());
  EXPECT_TRUE(EndpointExistsInCache(kOrigin_, kGroup_, kEndpoint_));
}

TEST_F(ReportingHeaderParserTest, IncludeSubdomainsFalse) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{kEndpoint_}};

  std::string header = ConstructHeaderGroupString(
      MakeEndpointGroup(kGroup_, endpoints, OriginSubdomains::EXCLUDE),
      false /* omit_defaults */);
  ParseHeader(kUrl_, header);

  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::EXCLUDE));
  EXPECT_EQ(1u, cache()->GetEndpointCount());
  EXPECT_TRUE(EndpointExistsInCache(kOrigin_, kGroup_, kEndpoint_));
}

TEST_F(ReportingHeaderParserTest, IncludeSubdomainsNotBoolean) {
  std::string header =
      "{\"group\": \"" + kGroup_ +
      "\", "
      "\"max_age\":86400, \"include_subdomains\": \"NotABoolean\", "
      "\"endpoints\": [{\"url\":\"" +
      kEndpoint_.spec() + "\"}]}";
  ParseHeader(kUrl_, header);

  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_EQ(1u, cache()->GetEndpointCount());
  EXPECT_TRUE(EndpointExistsInCache(kOrigin_, kGroup_, kEndpoint_));
}

TEST_F(ReportingHeaderParserTest, NonDefaultPriority) {
  const int kNonDefaultPriority = 10;
  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {
      {kEndpoint_, kNonDefaultPriority}};

  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints));
  ParseHeader(kUrl_, header);

  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_EQ(1u, cache()->GetEndpointCount());
  ReportingEndpoint endpoint =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_);
  ASSERT_TRUE(endpoint);
  EXPECT_EQ(kNonDefaultPriority, endpoint.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint.info.weight);
}

TEST_F(ReportingHeaderParserTest, NonDefaultWeight) {
  const int kNonDefaultWeight = 10;
  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {
      {kEndpoint_, ReportingEndpoint::EndpointInfo::kDefaultPriority,
       kNonDefaultWeight}};

  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints));
  ParseHeader(kUrl_, header);

  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_EQ(1u, cache()->GetEndpointCount());
  ReportingEndpoint endpoint =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_);
  ASSERT_TRUE(endpoint);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint.info.priority);
  EXPECT_EQ(kNonDefaultWeight, endpoint.info.weight);
}

TEST_F(ReportingHeaderParserTest, MaxAge) {
  const int kMaxAgeSecs = 100;
  base::TimeDelta ttl = base::TimeDelta::FromSeconds(kMaxAgeSecs);
  base::Time expires = clock()->Now() + ttl;

  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{kEndpoint_}};

  std::string header = ConstructHeaderGroupString(
      MakeEndpointGroup(kGroup_, endpoints, OriginSubdomains::DEFAULT, ttl));

  ParseHeader(kUrl_, header);
  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin_, kGroup_,
                                         OriginSubdomains::DEFAULT, expires));
}

TEST_F(ReportingHeaderParserTest, MultipleEndpointsSameGroup) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{kEndpoint_},
                                                            {kEndpoint2_}};
  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints));

  ParseHeader(kUrl_, header);
  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(2u, cache()->GetEndpointCount());
  ReportingEndpoint endpoint =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_);
  ASSERT_TRUE(endpoint);
  EXPECT_EQ(kOrigin_, endpoint.group_key.origin);
  EXPECT_EQ(kGroup_, endpoint.group_key.group_name);
  EXPECT_EQ(kEndpoint_, endpoint.info.url);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint.info.weight);

  ReportingEndpoint endpoint2 =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint2_);
  ASSERT_TRUE(endpoint2);
  EXPECT_EQ(kOrigin_, endpoint2.group_key.origin);
  EXPECT_EQ(kGroup_, endpoint2.group_key.group_name);
  EXPECT_EQ(kEndpoint2_, endpoint2.info.url);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint2.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint2.info.weight);
}

TEST_F(ReportingHeaderParserTest, MultipleEndpointsDifferentGroups) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints1 = {{kEndpoint_}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints2 = {{kEndpoint_}};
  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints1)) +
      ", " +
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup2_, endpoints2));

  ParseHeader(kUrl_, header);
  EXPECT_EQ(2u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin_, kGroup2_,
                                         OriginSubdomains::DEFAULT));
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));

  EXPECT_EQ(2u, cache()->GetEndpointCount());
  ReportingEndpoint endpoint =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_);
  ASSERT_TRUE(endpoint);
  EXPECT_EQ(kOrigin_, endpoint.group_key.origin);
  EXPECT_EQ(kGroup_, endpoint.group_key.group_name);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint.info.weight);

  ReportingEndpoint endpoint2 =
      FindEndpointInCache(kOrigin_, kGroup2_, kEndpoint_);
  ASSERT_TRUE(endpoint2);
  EXPECT_EQ(kOrigin_, endpoint2.group_key.origin);
  EXPECT_EQ(kGroup2_, endpoint2.group_key.group_name);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint2.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint2.info.weight);
}

TEST_F(ReportingHeaderParserTest, MultipleHeadersFromDifferentOrigins) {
  // First origin sets a header with two endpoints in the same group.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints1 = {{kEndpoint_},
                                                             {kEndpoint2_}};
  std::string header1 =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints1));
  ParseHeader(kUrl_, header1);

  // Second origin has two endpoint groups.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints2 = {{kEndpoint_}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints3 = {{kEndpoint2_}};
  std::string header2 =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints2)) +
      ", " +
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup2_, endpoints3));
  ParseHeader(kUrl2_, header2);

  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin2_));

  EXPECT_EQ(3u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin2_, kGroup_,
                                         OriginSubdomains::DEFAULT));
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin2_, kGroup2_,
                                         OriginSubdomains::DEFAULT));

  EXPECT_EQ(4u, cache()->GetEndpointCount());
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_));
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, kGroup_, kEndpoint2_));
  EXPECT_TRUE(FindEndpointInCache(kOrigin2_, kGroup_, kEndpoint_));
  EXPECT_TRUE(FindEndpointInCache(kOrigin2_, kGroup2_, kEndpoint2_));
}

TEST_F(ReportingHeaderParserTest,
       HeaderErroneouslyContainsMultipleGroupsOfSameName) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints1 = {{kEndpoint_}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints2 = {{kEndpoint2_}};
  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints1)) +
      ", " + ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints2));

  ParseHeader(kUrl_, header);
  // Result is as if they set the two groups with the same name as one group.
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());

  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));

  EXPECT_EQ(2u, cache()->GetEndpointCount());
  ReportingEndpoint endpoint =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_);
  ASSERT_TRUE(endpoint);
  EXPECT_EQ(kOrigin_, endpoint.group_key.origin);
  EXPECT_EQ(kGroup_, endpoint.group_key.group_name);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint.info.weight);

  ReportingEndpoint endpoint2 =
      FindEndpointInCache(kOrigin_, kGroup_, kEndpoint2_);
  ASSERT_TRUE(endpoint2);
  EXPECT_EQ(kOrigin_, endpoint2.group_key.origin);
  EXPECT_EQ(kGroup_, endpoint2.group_key.group_name);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultPriority,
            endpoint2.info.priority);
  EXPECT_EQ(ReportingEndpoint::EndpointInfo::kDefaultWeight,
            endpoint2.info.weight);
}

TEST_F(ReportingHeaderParserTest, OverwriteOldHeader) {
  // First, the origin sets a header with two endpoints in the same group.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints1 = {
      {kEndpoint_, 10 /* priority */}, {kEndpoint2_}};
  std::string header1 =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints1));
  ParseHeader(kUrl_, header1);

  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_EQ(2u, cache()->GetEndpointCount());
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_));
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, kGroup_, kEndpoint2_));

  // Second header from the same origin should overwrite the previous one.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints2 = {
      // This endpoint should update the priority of the existing one.
      {kEndpoint_, 20 /* priority */}};
  // The second endpoint in this group will be deleted.
  // This group is new.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints3 = {{kEndpoint2_}};
  std::string header2 =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints2)) +
      ", " +
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup2_, endpoints3));
  ParseHeader(kUrl_, header2);

  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));

  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin_, kGroup2_,
                                         OriginSubdomains::DEFAULT));

  EXPECT_EQ(2u, cache()->GetEndpointCount());
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_));
  EXPECT_EQ(20,
            FindEndpointInCache(kOrigin_, kGroup_, kEndpoint_).info.priority);
  EXPECT_FALSE(FindEndpointInCache(kOrigin_, kGroup_, kEndpoint2_));
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, kGroup2_, kEndpoint2_));
}

TEST_F(ReportingHeaderParserTest, OverwriteOldHeaderWithCompletelyNew) {
  std::vector<ReportingEndpoint::EndpointInfo> endpoints1_1 = {{MakeURL(10)},
                                                               {MakeURL(11)}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints2_1 = {{MakeURL(20)},
                                                               {MakeURL(21)}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints3_1 = {{MakeURL(30)},
                                                               {MakeURL(31)}};
  std::string header1 =
      ConstructHeaderGroupString(MakeEndpointGroup("1", endpoints1_1)) + ", " +
      ConstructHeaderGroupString(MakeEndpointGroup("2", endpoints2_1)) + ", " +
      ConstructHeaderGroupString(MakeEndpointGroup("3", endpoints3_1));
  ParseHeader(kUrl_, header1);
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(3u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "1", OriginSubdomains::DEFAULT));
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "2", OriginSubdomains::DEFAULT));
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "3", OriginSubdomains::DEFAULT));
  EXPECT_EQ(6u, cache()->GetEndpointCount());

  // Replace endpoints in each group with completely new endpoints.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints1_2 = {{MakeURL(12)}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints2_2 = {{MakeURL(22)}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints3_2 = {{MakeURL(32)}};
  std::string header2 =
      ConstructHeaderGroupString(MakeEndpointGroup("1", endpoints1_2)) + ", " +
      ConstructHeaderGroupString(MakeEndpointGroup("2", endpoints2_2)) + ", " +
      ConstructHeaderGroupString(MakeEndpointGroup("3", endpoints3_2));
  ParseHeader(kUrl_, header2);
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(3u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "1", OriginSubdomains::DEFAULT));
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "2", OriginSubdomains::DEFAULT));
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "3", OriginSubdomains::DEFAULT));
  EXPECT_EQ(3u, cache()->GetEndpointCount());
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, "1", MakeURL(12)));
  EXPECT_FALSE(FindEndpointInCache(kOrigin_, "1", MakeURL(10)));
  EXPECT_FALSE(FindEndpointInCache(kOrigin_, "1", MakeURL(11)));
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, "2", MakeURL(22)));
  EXPECT_FALSE(FindEndpointInCache(kOrigin_, "2", MakeURL(20)));
  EXPECT_FALSE(FindEndpointInCache(kOrigin_, "2", MakeURL(21)));
  EXPECT_TRUE(FindEndpointInCache(kOrigin_, "3", MakeURL(32)));
  EXPECT_FALSE(FindEndpointInCache(kOrigin_, "3", MakeURL(30)));
  EXPECT_FALSE(FindEndpointInCache(kOrigin_, "3", MakeURL(31)));

  // Replace all the groups with completely new groups.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints4_3 = {{MakeURL(40)}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints5_3 = {{MakeURL(50)}};
  std::string header3 =
      ConstructHeaderGroupString(MakeEndpointGroup("4", endpoints4_3)) + ", " +
      ConstructHeaderGroupString(MakeEndpointGroup("5", endpoints5_3));
  ParseHeader(kUrl_, header3);
  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(2u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "4", OriginSubdomains::DEFAULT));
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, "5", OriginSubdomains::DEFAULT));
  EXPECT_FALSE(
      EndpointGroupExistsInCache(kOrigin_, "1", OriginSubdomains::DEFAULT));
  EXPECT_FALSE(
      EndpointGroupExistsInCache(kOrigin_, "2", OriginSubdomains::DEFAULT));
  EXPECT_FALSE(
      EndpointGroupExistsInCache(kOrigin_, "3", OriginSubdomains::DEFAULT));
  EXPECT_EQ(2u, cache()->GetEndpointCount());
}

TEST_F(ReportingHeaderParserTest, ZeroMaxAgeRemovesEndpointGroup) {
  // Without a pre-existing client, max_age: 0 should do nothing.
  ASSERT_EQ(0u, cache()->GetEndpointCount());
  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\"}],\"max_age\":0}");
  EXPECT_EQ(0u, cache()->GetEndpointCount());

  // Set a header with two endpoint groups.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints1 = {{kEndpoint_}};
  std::vector<ReportingEndpoint::EndpointInfo> endpoints2 = {{kEndpoint2_}};
  std::string header1 =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints1)) +
      ", " +
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup2_, endpoints2));
  ParseHeader(kUrl_, header1);

  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(2u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_TRUE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin_, kGroup2_,
                                         OriginSubdomains::DEFAULT));
  EXPECT_EQ(2u, cache()->GetEndpointCount());

  // Set another header with max_age: 0 to delete one of the groups.
  std::string header2 = ConstructHeaderGroupString(MakeEndpointGroup(
                            kGroup_, endpoints1, OriginSubdomains::DEFAULT,
                            base::TimeDelta::FromSeconds(0))) +
                        ", " +
                        ConstructHeaderGroupString(MakeEndpointGroup(
                            kGroup2_, endpoints2));  // Other group stays.
  ParseHeader(kUrl_, header2);

  EXPECT_TRUE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(1u, cache()->GetEndpointGroupCountForTesting());

  // Group was deleted.
  EXPECT_FALSE(
      EndpointGroupExistsInCache(kOrigin_, kGroup_, OriginSubdomains::DEFAULT));
  // Other group remains in the cache.
  EXPECT_TRUE(EndpointGroupExistsInCache(kOrigin_, kGroup2_,
                                         OriginSubdomains::DEFAULT));
  EXPECT_EQ(1u, cache()->GetEndpointCount());

  // Set another header with max_age: 0 to delete the other group. (Should work
  // even if the endpoints field is an empty list.)
  std::string header3 = ConstructHeaderGroupString(MakeEndpointGroup(
      kGroup2_, std::vector<ReportingEndpoint::EndpointInfo>(),
      OriginSubdomains::DEFAULT, base::TimeDelta::FromSeconds(0)));
  ParseHeader(kUrl_, header3);

  // Deletion of the last remaining group also deletes the client for this
  // origin.
  EXPECT_FALSE(OriginClientExistsInCache(kOrigin_));
  EXPECT_EQ(0u, cache()->GetEndpointGroupCountForTesting());
  EXPECT_EQ(0u, cache()->GetEndpointCount());
}

TEST_F(ReportingHeaderParserTest, EvictEndpointsOverPerOriginLimit1) {
  // Set a header with too many endpoints, all in the same group.
  std::vector<ReportingEndpoint::EndpointInfo> endpoints;
  for (size_t i = 0; i < policy().max_endpoints_per_origin + 1; ++i) {
    endpoints.push_back({MakeURL(i)});
  }
  std::string header =
      ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints));
  ParseHeader(kUrl_, header);

  // Endpoint count should be at most the limit.
  EXPECT_GE(policy().max_endpoints_per_origin, cache()->GetEndpointCount());
}

TEST_F(ReportingHeaderParserTest, EvictEndpointsOverPerOriginLimit2) {
  // Set a header with too many endpoints, in different groups.
  std::string header;
  for (size_t i = 0; i < policy().max_endpoints_per_origin + 1; ++i) {
    std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{MakeURL(i)}};
    header = header + ConstructHeaderGroupString(MakeEndpointGroup(
                          base::NumberToString(i), endpoints));
    if (i != policy().max_endpoints_per_origin)
      header = header + ", ";
  }
  ParseHeader(kUrl_, header);

  // Endpoint count should be at most the limit.
  EXPECT_GE(policy().max_endpoints_per_origin, cache()->GetEndpointCount());
}

TEST_F(ReportingHeaderParserTest, EvictEndpointsOverGlobalLimit) {
  // Set headers from different origins up to the global limit.
  for (size_t i = 0; i < policy().max_endpoint_count; ++i) {
    std::vector<ReportingEndpoint::EndpointInfo> endpoints = {{MakeURL(i)}};
    std::string header =
        ConstructHeaderGroupString(MakeEndpointGroup(kGroup_, endpoints));
    ParseHeader(MakeURL(i), header);
  }
  EXPECT_EQ(policy().max_endpoint_count, cache()->GetEndpointCount());

  // Parse one more header to trigger eviction.
  ParseHeader(kUrl_, "{\"endpoints\":[{\"url\":\"" + kEndpoint_.spec() +
                         "\"}],\"max_age\":1}");

  // Endpoint count should be at most the limit.
  EXPECT_GE(policy().max_endpoint_count, cache()->GetEndpointCount());
}

}  // namespace
}  // namespace net
