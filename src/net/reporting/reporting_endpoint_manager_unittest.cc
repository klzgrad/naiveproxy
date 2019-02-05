// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/reporting/reporting_endpoint_manager.h"

#include <string>

#include "base/test/simple_test_tick_clock.h"
#include "base/time/time.h"
#include "net/base/backoff_entry.h"
#include "net/reporting/reporting_cache.h"
#include "net/reporting/reporting_client.h"
#include "net/reporting/reporting_policy.h"
#include "net/reporting/reporting_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace net {
namespace {

class ReportingEndpointManagerTest : public ReportingTestBase {
 public:
  ReportingEndpointManagerTest() {
    ReportingPolicy policy;
    policy.endpoint_backoff_policy.num_errors_to_ignore = 0;
    policy.endpoint_backoff_policy.initial_delay_ms = 60000;
    policy.endpoint_backoff_policy.multiply_factor = 2.0;
    policy.endpoint_backoff_policy.jitter_factor = 0.0;
    policy.endpoint_backoff_policy.maximum_backoff_ms = -1;
    policy.endpoint_backoff_policy.entry_lifetime_ms = 0;
    policy.endpoint_backoff_policy.always_use_initial_delay = false;
    UsePolicy(policy);
  }

 protected:
  void SetClient(const GURL& endpoint, int priority, int weight) {
    cache()->SetClient(kOrigin_, endpoint, ReportingClient::Subdomains::EXCLUDE,
                       kGroup_, tomorrow(), priority, weight);
  }

  const url::Origin kOrigin_ = url::Origin::Create(GURL("https://origin/"));
  const GURL kEndpoint_ = GURL("https://endpoint/");
  const std::string kGroup_ = "group";
};

TEST_F(ReportingEndpointManagerTest, NoEndpoint) {
  const ReportingClient* client =
      endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  EXPECT_TRUE(client == nullptr);
}

TEST_F(ReportingEndpointManagerTest, Endpoint) {
  SetClient(kEndpoint_, ReportingClient::kDefaultPriority,
            ReportingClient::kDefaultWeight);

  const ReportingClient* client =
      endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  ASSERT_TRUE(client != nullptr);
  EXPECT_EQ(kEndpoint_, client->endpoint);
}

TEST_F(ReportingEndpointManagerTest, ExpiredEndpoint) {
  SetClient(kEndpoint_, ReportingClient::kDefaultPriority,
            ReportingClient::kDefaultWeight);

  // Default expiration is "tomorrow", so make sure we're past that.
  tick_clock()->Advance(base::TimeDelta::FromDays(2));

  const ReportingClient* client =
      endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  EXPECT_TRUE(client == nullptr);
}

TEST_F(ReportingEndpointManagerTest, BackedOffEndpoint) {
  ASSERT_EQ(2.0, policy().endpoint_backoff_policy.multiply_factor);

  base::TimeDelta initial_delay = base::TimeDelta::FromMilliseconds(
      policy().endpoint_backoff_policy.initial_delay_ms);

  SetClient(kEndpoint_, ReportingClient::kDefaultPriority,
            ReportingClient::kDefaultWeight);

  endpoint_manager()->InformOfEndpointRequest(kEndpoint_, false);

  // After one failure, endpoint is in exponential backoff.
  const ReportingClient* client =
      endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  EXPECT_TRUE(client == nullptr);

  // After initial delay, endpoint is usable again.
  tick_clock()->Advance(initial_delay);

  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  ASSERT_TRUE(client != nullptr);
  EXPECT_EQ(kEndpoint_, client->endpoint);

  endpoint_manager()->InformOfEndpointRequest(kEndpoint_, false);

  // After a second failure, endpoint is backed off again.
  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  EXPECT_TRUE(client == nullptr);

  tick_clock()->Advance(initial_delay);

  // Next backoff is longer -- 2x the first -- so endpoint isn't usable yet.
  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  EXPECT_TRUE(client == nullptr);

  tick_clock()->Advance(initial_delay);

  // After 2x the initial delay, the endpoint is usable again.
  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  ASSERT_TRUE(client != nullptr);
  EXPECT_EQ(kEndpoint_, client->endpoint);

  endpoint_manager()->InformOfEndpointRequest(kEndpoint_, true);
  endpoint_manager()->InformOfEndpointRequest(kEndpoint_, true);

  // Two more successful requests should reset the backoff to the initial delay
  // again.
  endpoint_manager()->InformOfEndpointRequest(kEndpoint_, false);

  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  EXPECT_TRUE(client == nullptr);

  tick_clock()->Advance(initial_delay);

  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  EXPECT_TRUE(client != nullptr);
}

// Make sure that multiple endpoints will all be returned at some point, to
// avoid accidentally or intentionally implementing any priority ordering.
TEST_F(ReportingEndpointManagerTest, RandomEndpoint) {
  static const GURL kEndpoint1("https://endpoint1/");
  static const GURL kEndpoint2("https://endpoint2/");
  static const int kMaxAttempts = 20;

  SetClient(kEndpoint1, ReportingClient::kDefaultPriority,
            ReportingClient::kDefaultWeight);
  SetClient(kEndpoint2, ReportingClient::kDefaultPriority,
            ReportingClient::kDefaultWeight);

  bool endpoint1_seen = false;
  bool endpoint2_seen = false;

  for (int i = 0; i < kMaxAttempts; ++i) {
    const ReportingClient* client =
        endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
    ASSERT_TRUE(client != nullptr);
    ASSERT_TRUE(client->endpoint == kEndpoint1 ||
                client->endpoint == kEndpoint2);

    if (client->endpoint == kEndpoint1)
      endpoint1_seen = true;
    else if (client->endpoint == kEndpoint2)
      endpoint2_seen = true;

    if (endpoint1_seen && endpoint2_seen)
      break;
  }

  EXPECT_TRUE(endpoint1_seen);
  EXPECT_TRUE(endpoint2_seen);
}

TEST_F(ReportingEndpointManagerTest, Priority) {
  static const GURL kPrimaryEndpoint("https://endpoint1/");
  static const GURL kBackupEndpoint("https://endpoint2/");

  SetClient(kPrimaryEndpoint, 10, ReportingClient::kDefaultWeight);
  SetClient(kBackupEndpoint, 20, ReportingClient::kDefaultWeight);

  const ReportingClient* client =
      endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  ASSERT_TRUE(client != nullptr);
  EXPECT_EQ(kPrimaryEndpoint, client->endpoint);

  // The backoff policy we set up in the constructor means that a single failed
  // upload will take the primary endpoint out of contention.  This should cause
  // us to choose the backend endpoint.
  endpoint_manager()->InformOfEndpointRequest(kPrimaryEndpoint, false);
  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  ASSERT_TRUE(client != nullptr);
  EXPECT_EQ(kBackupEndpoint, client->endpoint);

  // Advance the current time far enough to clear out the primary endpoint's
  // backoff clock.  This should bring the primary endpoint back into play.
  tick_clock()->Advance(base::TimeDelta::FromMinutes(2));
  client = endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
  ASSERT_TRUE(client != nullptr);
  EXPECT_EQ(kPrimaryEndpoint, client->endpoint);
}

// Note: This test depends on the deterministic mock RandIntCallback set up in
// TestReportingContext, which returns consecutive integers starting at 0
// (modulo the requested range, plus the requested minimum).
TEST_F(ReportingEndpointManagerTest, Weight) {
  static const GURL kEndpoint1("https://endpoint1/");
  static const GURL kEndpoint2("https://endpoint2/");

  static const int kEndpoint1Weight = 5;
  static const int kEndpoint2Weight = 2;
  static const int kTotalEndpointWeight = kEndpoint1Weight + kEndpoint2Weight;

  SetClient(kEndpoint1, ReportingClient::kDefaultPriority, kEndpoint1Weight);
  SetClient(kEndpoint2, ReportingClient::kDefaultPriority, kEndpoint2Weight);

  int endpoint1_count = 0;
  int endpoint2_count = 0;

  for (int i = 0; i < kTotalEndpointWeight; ++i) {
    const ReportingClient* client =
        endpoint_manager()->FindClientForOriginAndGroup(kOrigin_, kGroup_);
    ASSERT_TRUE(client != nullptr);
    ASSERT_TRUE(client->endpoint == kEndpoint1 ||
                client->endpoint == kEndpoint2);

    if (client->endpoint == kEndpoint1)
      ++endpoint1_count;
    else if (client->endpoint == kEndpoint2)
      ++endpoint2_count;
  }

  EXPECT_EQ(kEndpoint1Weight, endpoint1_count);
  EXPECT_EQ(kEndpoint2Weight, endpoint2_count);
}

}  // namespace
}  // namespace net
