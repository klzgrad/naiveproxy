// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_header_list.h"

#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

using ::testing::ElementsAre;
using ::testing::Pair;

namespace quic {

class QuicHeaderListTest : public QuicTest {};

// This test verifies that QuicHeaderList accumulates header pairs in order.
TEST_F(QuicHeaderListTest, OnHeader) {
  QuicHeaderList headers;
  headers.OnHeader("foo", "bar");
  headers.OnHeader("april", "fools");
  headers.OnHeader("beep", "");

  EXPECT_THAT(headers, ElementsAre(Pair("foo", "bar"), Pair("april", "fools"),
                                   Pair("beep", "")));
}

TEST_F(QuicHeaderListTest, DebugString) {
  QuicHeaderList headers;
  headers.OnHeader("foo", "bar");
  headers.OnHeader("april", "fools");
  headers.OnHeader("beep", "");

  EXPECT_EQ("{ foo=bar, april=fools, beep=, }", headers.DebugString());
}

TEST_F(QuicHeaderListTest, TooLarge) {
  const size_t kMaxHeaderListSize = 256;

  QuicHeaderList headers;
  headers.set_max_header_list_size(kMaxHeaderListSize);
  std::string key = "key";
  std::string value(kMaxHeaderListSize, '1');
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
  std::string key = "key";
  std::string value(1 << 18, '1');
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

  EXPECT_THAT(headers2, ElementsAre(Pair("foo", "bar"), Pair("april", "fools"),
                                    Pair("beep", "")));
  EXPECT_THAT(headers3, ElementsAre(Pair("foo", "bar"), Pair("april", "fools"),
                                    Pair("beep", "")));
}

}  // namespace quic
