// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/value_splitting_header_list.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

TEST(ValueSplittingHeaderListTest, Comparison) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = quiche::QuicheStringPiece("bar\0baz", 7);
  block["baz"] = "qux";
  block["cookie"] = "foo; bar";

  ValueSplittingHeaderList headers(&block);
  ValueSplittingHeaderList::const_iterator it1 = headers.begin();
  const int kEnd = 6;
  for (int i = 0; i < kEnd; ++i) {
    // Compare to begin().
    if (i == 0) {
      EXPECT_TRUE(it1 == headers.begin());
      EXPECT_TRUE(headers.begin() == it1);
      EXPECT_FALSE(it1 != headers.begin());
      EXPECT_FALSE(headers.begin() != it1);
    } else {
      EXPECT_FALSE(it1 == headers.begin());
      EXPECT_FALSE(headers.begin() == it1);
      EXPECT_TRUE(it1 != headers.begin());
      EXPECT_TRUE(headers.begin() != it1);
    }

    // Compare to end().
    if (i == kEnd - 1) {
      EXPECT_TRUE(it1 == headers.end());
      EXPECT_TRUE(headers.end() == it1);
      EXPECT_FALSE(it1 != headers.end());
      EXPECT_FALSE(headers.end() != it1);
    } else {
      EXPECT_FALSE(it1 == headers.end());
      EXPECT_FALSE(headers.end() == it1);
      EXPECT_TRUE(it1 != headers.end());
      EXPECT_TRUE(headers.end() != it1);
    }

    // Compare to another iterator walking through the container.
    ValueSplittingHeaderList::const_iterator it2 = headers.begin();
    for (int j = 0; j < kEnd; ++j) {
      if (i == j) {
        EXPECT_TRUE(it1 == it2);
        EXPECT_FALSE(it1 != it2);
      } else {
        EXPECT_FALSE(it1 == it2);
        EXPECT_TRUE(it1 != it2);
      }
      if (j < kEnd - 1) {
        ASSERT_NE(it2, headers.end());
        ++it2;
      }
    }

    if (i < kEnd - 1) {
      ASSERT_NE(it1, headers.end());
      ++it1;
    }
  }
}

TEST(ValueSplittingHeaderListTest, Empty) {
  spdy::SpdyHeaderBlock block;

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(headers, ElementsAre());
  EXPECT_EQ(headers.begin(), headers.end());
}

TEST(ValueSplittingHeaderListTest, Split) {
  struct {
    const char* name;
    quiche::QuicheStringPiece value;
    std::vector<const char*> expected_values;
  } kTestData[]{
      // Empty value.
      {"foo", "", {""}},
      // Trivial case.
      {"foo", "bar", {"bar"}},
      // Simple split.
      {"foo", {"bar\0baz", 7}, {"bar", "baz"}},
      {"cookie", "foo;bar", {"foo", "bar"}},
      {"cookie", "foo; bar", {"foo", "bar"}},
      // Empty fragments with \0 separator.
      {"foo", {"\0", 1}, {"", ""}},
      {"bar", {"foo\0", 4}, {"foo", ""}},
      {"baz", {"\0bar", 4}, {"", "bar"}},
      {"qux", {"\0foobar\0", 8}, {"", "foobar", ""}},
      // Empty fragments with ";" separator.
      {"cookie", ";", {"", ""}},
      {"cookie", "foo;", {"foo", ""}},
      {"cookie", ";bar", {"", "bar"}},
      {"cookie", ";foobar;", {"", "foobar", ""}},
      // Empty fragments with "; " separator.
      {"cookie", "; ", {"", ""}},
      {"cookie", "foo; ", {"foo", ""}},
      {"cookie", "; bar", {"", "bar"}},
      {"cookie", "; foobar; ", {"", "foobar", ""}},
  };

  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kTestData); ++i) {
    spdy::SpdyHeaderBlock block;
    block[kTestData[i].name] = kTestData[i].value;

    ValueSplittingHeaderList headers(&block);
    auto it = headers.begin();
    for (const char* expected_value : kTestData[i].expected_values) {
      ASSERT_NE(it, headers.end());
      EXPECT_EQ(it->first, kTestData[i].name);
      EXPECT_EQ(it->second, expected_value);
      ++it;
    }
    EXPECT_EQ(it, headers.end());
  }
}

TEST(ValueSplittingHeaderListTest, MultipleFields) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = quiche::QuicheStringPiece("bar\0baz\0", 8);
  block["cookie"] = "foo; bar";
  block["bar"] = quiche::QuicheStringPiece("qux\0foo", 7);

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(headers, ElementsAre(Pair("foo", "bar"), Pair("foo", "baz"),
                                   Pair("foo", ""), Pair("cookie", "foo"),
                                   Pair("cookie", "bar"), Pair("bar", "qux"),
                                   Pair("bar", "foo")));
}

TEST(ValueSplittingHeaderListTest, CookieStartsWithSpace) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = "bar";
  block["cookie"] = " foo";
  block["bar"] = "baz";

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(headers, ElementsAre(Pair("foo", "bar"), Pair("cookie", " foo"),
                                   Pair("bar", "baz")));
}

}  // namespace
}  // namespace test
}  // namespace quic
