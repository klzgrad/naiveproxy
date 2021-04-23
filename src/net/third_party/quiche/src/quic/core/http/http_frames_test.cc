// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/http/http_frames.h"

#include <sstream>

#include "quic/core/http/http_constants.h"
#include "quic/platform/api/quic_test.h"

namespace quic {
namespace test {

TEST(HttpFramesTest, CancelPushFrame) {
  CancelPushFrame a{1};
  EXPECT_TRUE(a == a);

  CancelPushFrame b{1};
  EXPECT_TRUE(a == b);

  b.push_id = 2;
  EXPECT_FALSE(a == b);
}

TEST(HttpFramesTest, SettingsFrame) {
  SettingsFrame a;
  EXPECT_TRUE(a == a);
  EXPECT_EQ("", a.ToString());

  SettingsFrame b;
  b.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 1;
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(b == b);

  a.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 2;
  EXPECT_FALSE(a == b);
  a.values[SETTINGS_QPACK_MAX_TABLE_CAPACITY] = 1;
  EXPECT_TRUE(a == b);

  EXPECT_EQ("SETTINGS_QPACK_MAX_TABLE_CAPACITY = 1; ", b.ToString());
  std::stringstream s;
  s << b;
  EXPECT_EQ("SETTINGS_QPACK_MAX_TABLE_CAPACITY = 1; ", s.str());
}

TEST(HttpFramesTest, PushPromiseFrame) {
  PushPromiseFrame a{1, ""};
  EXPECT_TRUE(a == a);

  PushPromiseFrame b{2, ""};
  EXPECT_FALSE(a == b);

  b.push_id = 1;
  EXPECT_TRUE(a == b);

  b.headers = "foo";
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(b == b);
}

TEST(HttpFramesTest, GoAwayFrame) {
  GoAwayFrame a{1};
  EXPECT_TRUE(a == a);

  GoAwayFrame b{2};
  EXPECT_FALSE(a == b);

  b.id = 1;
  EXPECT_TRUE(a == b);
}

TEST(HttpFramesTest, MaxPushIdFrame) {
  MaxPushIdFrame a{1};
  EXPECT_TRUE(a == a);

  MaxPushIdFrame b{2};
  EXPECT_FALSE(a == b);

  b.push_id = 1;
  EXPECT_TRUE(a == b);
}

TEST(HttpFramesTest, PriorityUpdateFrame) {
  PriorityUpdateFrame a{REQUEST_STREAM, 0, ""};
  EXPECT_TRUE(a == a);
  PriorityUpdateFrame b{REQUEST_STREAM, 4, ""};
  EXPECT_FALSE(a == b);
  a.prioritized_element_id = 4;
  EXPECT_TRUE(a == b);

  a.prioritized_element_type = PUSH_STREAM;
  EXPECT_FALSE(a == b);
  b.prioritized_element_type = PUSH_STREAM;
  EXPECT_TRUE(a == b);

  a.priority_field_value = "foo";
  EXPECT_FALSE(a == b);

  EXPECT_EQ(
      "Priority Frame : {prioritized_element_type: 128, "
      "prioritized_element_id: 4, priority_field_value: foo}",
      a.ToString());
  std::stringstream s;
  s << a;
  EXPECT_EQ(
      "Priority Frame : {prioritized_element_type: 128, "
      "prioritized_element_id: 4, priority_field_value: foo}",
      s.str());
}

TEST(HttpFramesTest, AcceptChFrame) {
  AcceptChFrame a;
  EXPECT_TRUE(a == a);
  EXPECT_EQ("ACCEPT_CH frame with 0 entries: ", a.ToString());

  AcceptChFrame b{{{"foo", "bar"}}};
  EXPECT_FALSE(a == b);

  a.entries.push_back({"foo", "bar"});
  EXPECT_TRUE(a == b);

  EXPECT_EQ("ACCEPT_CH frame with 1 entries: origin: foo; value: bar",
            a.ToString());
  std::stringstream s;
  s << a;
  EXPECT_EQ("ACCEPT_CH frame with 1 entries: origin: foo; value: bar", s.str());
}

}  // namespace test
}  // namespace quic
