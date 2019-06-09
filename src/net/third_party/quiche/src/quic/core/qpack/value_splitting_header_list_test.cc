// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/value_splitting_header_list.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

using ::testing::ElementsAre;
using ::testing::Pair;

TEST(ValueSplittingHeaderListTest, Comparison) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = QuicStringPiece("bar\0baz", 7);
  block["baz"] = "qux";

  ValueSplittingHeaderList headers(&block);
  ValueSplittingHeaderList::const_iterator it1 = headers.begin();
  const int kEnd = 4;
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
      ++it2;
    }

    ++it1;
  }
}

TEST(ValueSplittingHeaderListTest, Empty) {
  spdy::SpdyHeaderBlock block;

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(headers, ElementsAre());
  EXPECT_EQ(headers.begin(), headers.end());
}

TEST(ValueSplittingHeaderListTest, Simple) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = "bar";
  block["baz"] = "qux";

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(headers, ElementsAre(Pair("foo", "bar"), Pair("baz", "qux")));
  EXPECT_NE(headers.begin(), headers.end());
}

TEST(ValueSplittingHeaderListTest, EmptyValue) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = "";

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(headers, ElementsAre(Pair("foo", "")));
}

TEST(ValueSplittingHeaderListTest, SimpleSplit) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = QuicStringPiece("bar\0baz", 7);
  block["baz"] = QuicStringPiece("foo\0foo", 7);

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(headers, ElementsAre(Pair("foo", "bar"), Pair("foo", "baz"),
                                   Pair("baz", "foo"), Pair("baz", "foo")));
}

TEST(ValueSplittingHeaderListTest, EmptyFragments) {
  spdy::SpdyHeaderBlock block;
  block["foo"] = QuicStringPiece("\0", 1);
  block["bar"] = QuicStringPiece("foo\0", 4);
  block["baz"] = QuicStringPiece("\0bar", 4);
  block["qux"] = QuicStringPiece("\0foobar\0", 8);

  ValueSplittingHeaderList headers(&block);
  EXPECT_THAT(
      headers,
      ElementsAre(Pair("foo", ""), Pair("foo", ""), Pair("bar", "foo"),
                  Pair("bar", ""), Pair("baz", ""), Pair("baz", "bar"),
                  Pair("qux", ""), Pair("qux", "foobar"), Pair("qux", "")));
}

}  // namespace
}  // namespace test
}  // namespace quic
