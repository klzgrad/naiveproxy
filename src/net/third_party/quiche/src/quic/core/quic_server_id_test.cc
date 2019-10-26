// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_server_id.h"

#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_estimate_memory_usage.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {

namespace {

class QuicServerIdTest : public QuicTest {};

TEST_F(QuicServerIdTest, Constructor) {
  QuicServerId google_server_id("google.com", 10, false);
  EXPECT_EQ("google.com", google_server_id.host());
  EXPECT_EQ(10, google_server_id.port());
  EXPECT_FALSE(google_server_id.privacy_mode_enabled());

  QuicServerId private_server_id("mail.google.com", 12, true);
  EXPECT_EQ("mail.google.com", private_server_id.host());
  EXPECT_EQ(12, private_server_id.port());
  EXPECT_TRUE(private_server_id.privacy_mode_enabled());
}

TEST_F(QuicServerIdTest, LessThan) {
  QuicServerId a_10_https("a.com", 10, false);
  QuicServerId a_11_https("a.com", 11, false);
  QuicServerId b_10_https("b.com", 10, false);
  QuicServerId b_11_https("b.com", 11, false);

  QuicServerId a_10_https_private("a.com", 10, true);
  QuicServerId a_11_https_private("a.com", 11, true);
  QuicServerId b_10_https_private("b.com", 10, true);
  QuicServerId b_11_https_private("b.com", 11, true);

  // Test combinations of host, port, and privacy being same on left and
  // right side of less than.
  EXPECT_FALSE(a_10_https < a_10_https);
  EXPECT_TRUE(a_10_https < a_10_https_private);
  EXPECT_FALSE(a_10_https_private < a_10_https);
  EXPECT_FALSE(a_10_https_private < a_10_https_private);

  // Test with either host, port or https being different on left and right side
  // of less than.
  bool left_privacy;
  bool right_privacy;
  for (int i = 0; i < 4; i++) {
    left_privacy = (i / 2 == 0);
    right_privacy = (i % 2 == 0);
    QuicServerId a_10_https_left_private("a.com", 10, left_privacy);
    QuicServerId a_10_https_right_private("a.com", 10, right_privacy);
    QuicServerId a_11_https_left_private("a.com", 11, left_privacy);
    QuicServerId a_11_https_right_private("a.com", 11, right_privacy);

    QuicServerId b_10_https_left_private("b.com", 10, left_privacy);
    QuicServerId b_10_https_right_private("b.com", 10, right_privacy);
    QuicServerId b_11_https_left_private("b.com", 11, left_privacy);
    QuicServerId b_11_https_right_private("b.com", 11, right_privacy);

    EXPECT_TRUE(a_10_https_left_private < a_11_https_right_private);
    EXPECT_TRUE(a_10_https_left_private < b_10_https_right_private);
    EXPECT_TRUE(a_10_https_left_private < b_11_https_right_private);
    EXPECT_FALSE(a_11_https_left_private < a_10_https_right_private);
    EXPECT_FALSE(a_11_https_left_private < b_10_https_right_private);
    EXPECT_TRUE(a_11_https_left_private < b_11_https_right_private);
    EXPECT_FALSE(b_10_https_left_private < a_10_https_right_private);
    EXPECT_TRUE(b_10_https_left_private < a_11_https_right_private);
    EXPECT_TRUE(b_10_https_left_private < b_11_https_right_private);
    EXPECT_FALSE(b_11_https_left_private < a_10_https_right_private);
    EXPECT_FALSE(b_11_https_left_private < a_11_https_right_private);
    EXPECT_FALSE(b_11_https_left_private < b_10_https_right_private);
  }
}

TEST_F(QuicServerIdTest, Equals) {
  bool left_privacy;
  bool right_privacy;
  for (int i = 0; i < 2; i++) {
    left_privacy = right_privacy = (i == 0);
    QuicServerId a_10_https_right_private("a.com", 10, right_privacy);
    QuicServerId a_11_https_right_private("a.com", 11, right_privacy);
    QuicServerId b_10_https_right_private("b.com", 10, right_privacy);
    QuicServerId b_11_https_right_private("b.com", 11, right_privacy);

    QuicServerId new_a_10_https_left_private("a.com", 10, left_privacy);
    QuicServerId new_a_11_https_left_private("a.com", 11, left_privacy);
    QuicServerId new_b_10_https_left_private("b.com", 10, left_privacy);
    QuicServerId new_b_11_https_left_private("b.com", 11, left_privacy);

    EXPECT_EQ(new_a_10_https_left_private, a_10_https_right_private);
    EXPECT_EQ(new_a_11_https_left_private, a_11_https_right_private);
    EXPECT_EQ(new_b_10_https_left_private, b_10_https_right_private);
    EXPECT_EQ(new_b_11_https_left_private, b_11_https_right_private);
  }

  for (int i = 0; i < 2; i++) {
    right_privacy = (i == 0);
    QuicServerId a_10_https_right_private("a.com", 10, right_privacy);
    QuicServerId a_11_https_right_private("a.com", 11, right_privacy);
    QuicServerId b_10_https_right_private("b.com", 10, right_privacy);
    QuicServerId b_11_https_right_private("b.com", 11, right_privacy);

    QuicServerId new_a_10_https_left_private("a.com", 10, false);

    EXPECT_FALSE(new_a_10_https_left_private == a_11_https_right_private);
    EXPECT_FALSE(new_a_10_https_left_private == b_10_https_right_private);
    EXPECT_FALSE(new_a_10_https_left_private == b_11_https_right_private);
  }
  QuicServerId a_10_https_private("a.com", 10, true);
  QuicServerId new_a_10_https_no_private("a.com", 10, false);
  EXPECT_FALSE(new_a_10_https_no_private == a_10_https_private);
}

TEST_F(QuicServerIdTest, EstimateMemoryUsage) {
  std::string host = "this is a rather very quite long hostname";
  uint16_t port = 10;
  bool privacy_mode_enabled = true;
  QuicServerId server_id(host, port, privacy_mode_enabled);
  EXPECT_EQ(QuicEstimateMemoryUsage(host), QuicEstimateMemoryUsage(server_id));
}

}  // namespace

}  // namespace quic
