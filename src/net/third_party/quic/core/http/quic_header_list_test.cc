// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/http/quic_header_list.h"

#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {

class QuicHeaderListTest : public QuicTest {};

// This test verifies that QuicHeaderList accumulates header pairs in order.
TEST_F(QuicHeaderListTest, OnHeader) {
  QuicHeaderList headers;
  headers.OnHeader("foo", "bar");
  headers.OnHeader("april", "fools");
  headers.OnHeader("beep", "");

  EXPECT_EQ("{ foo=bar, april=fools, beep=, }", headers.DebugString());
}

TEST_F(QuicHeaderListTest, TooLarge) {
  QuicHeaderList headers;
  QuicString key = "key";
  QuicString value(1 << 18, '1');
  // Send a header that exceeds max_header_list_size.
  headers.OnHeader(key, value);
  // Send a second header exceeding max_header_list_size.
  headers.OnHeader(key + "2", value);
  // We should not allocate more memory after exceeding max_header_list_size.
  EXPECT_LT(headers.DebugString().size(), 2 * value.size());
  size_t total_bytes = 2 * (key.size() + value.size()) + 1;
  headers.OnHeaderBlockEnd(total_bytes, total_bytes);
  EXPECT_TRUE(headers.empty());

  EXPECT_EQ("{ }", headers.DebugString());
}

TEST_F(QuicHeaderListTest, NotTooLarge) {
  QuicHeaderList headers;
  headers.set_max_header_list_size(1 << 20);
  QuicString key = "key";
  QuicString value(1 << 18, '1');
  headers.OnHeader(key, value);
  size_t total_bytes = key.size() + value.size();
  headers.OnHeaderBlockEnd(total_bytes, total_bytes);
  EXPECT_FALSE(headers.empty());
}

// This test verifies that QuicHeaderList is copyable and assignable.
TEST_F(QuicHeaderListTest, IsCopyableAndAssignable) {
  QuicHeaderList headers;
  headers.OnHeader("foo", "bar");
  headers.OnHeader("april", "fools");
  headers.OnHeader("beep", "");

  QuicHeaderList headers2(headers);
  QuicHeaderList headers3 = headers;

  EXPECT_EQ("{ foo=bar, april=fools, beep=, }", headers2.DebugString());
  EXPECT_EQ("{ foo=bar, april=fools, beep=, }", headers3.DebugString());
}

}  // namespace quic
