// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_prefixed_buffer_reader.h"

#include <string>

#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_piece.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_test.h"

namespace spdy {

namespace test {

using testing::ElementsAreArray;

class SpdyPrefixedBufferReaderTest : public ::testing::Test {
 protected:
  SpdyPrefixedBufferReader Build(const std::string& prefix,
                                 const std::string& suffix) {
    prefix_ = prefix;
    suffix_ = suffix;
    return SpdyPrefixedBufferReader(prefix_.data(), prefix_.length(),
                                    suffix_.data(), suffix_.length());
  }
  std::string prefix_, suffix_;
};

TEST_F(SpdyPrefixedBufferReaderTest, ReadRawFromPrefix) {
  SpdyPrefixedBufferReader reader = Build("foobar", "");
  EXPECT_EQ(6u, reader.Available());

  char buffer[] = "123456";
  EXPECT_FALSE(reader.ReadN(10, buffer));  // Not enough buffer.
  EXPECT_TRUE(reader.ReadN(6, buffer));
  EXPECT_THAT(buffer, ElementsAreArray("foobar"));
  EXPECT_EQ(0u, reader.Available());
}

TEST_F(SpdyPrefixedBufferReaderTest, ReadPieceFromPrefix) {
  SpdyPrefixedBufferReader reader = Build("foobar", "");
  EXPECT_EQ(6u, reader.Available());

  SpdyPinnableBufferPiece piece;
  EXPECT_FALSE(reader.ReadN(10, &piece));  // Not enough buffer.
  EXPECT_TRUE(reader.ReadN(6, &piece));
  EXPECT_FALSE(piece.IsPinned());
  EXPECT_EQ(SpdyStringPiece("foobar"), SpdyStringPiece(piece));
  EXPECT_EQ(0u, reader.Available());
}

TEST_F(SpdyPrefixedBufferReaderTest, ReadRawFromSuffix) {
  SpdyPrefixedBufferReader reader = Build("", "foobar");
  EXPECT_EQ(6u, reader.Available());

  char buffer[] = "123456";
  EXPECT_FALSE(reader.ReadN(10, buffer));  // Not enough buffer.
  EXPECT_TRUE(reader.ReadN(6, buffer));
  EXPECT_THAT(buffer, ElementsAreArray("foobar"));
  EXPECT_EQ(0u, reader.Available());
}

TEST_F(SpdyPrefixedBufferReaderTest, ReadPieceFromSuffix) {
  SpdyPrefixedBufferReader reader = Build("", "foobar");
  EXPECT_EQ(6u, reader.Available());

  SpdyPinnableBufferPiece piece;
  EXPECT_FALSE(reader.ReadN(10, &piece));  // Not enough buffer.
  EXPECT_TRUE(reader.ReadN(6, &piece));
  EXPECT_FALSE(piece.IsPinned());
  EXPECT_EQ(SpdyStringPiece("foobar"), SpdyStringPiece(piece));
  EXPECT_EQ(0u, reader.Available());
}

TEST_F(SpdyPrefixedBufferReaderTest, ReadRawSpanning) {
  SpdyPrefixedBufferReader reader = Build("foob", "ar");
  EXPECT_EQ(6u, reader.Available());

  char buffer[] = "123456";
  EXPECT_FALSE(reader.ReadN(10, buffer));  // Not enough buffer.
  EXPECT_TRUE(reader.ReadN(6, buffer));
  EXPECT_THAT(buffer, ElementsAreArray("foobar"));
  EXPECT_EQ(0u, reader.Available());
}

TEST_F(SpdyPrefixedBufferReaderTest, ReadPieceSpanning) {
  SpdyPrefixedBufferReader reader = Build("foob", "ar");
  EXPECT_EQ(6u, reader.Available());

  SpdyPinnableBufferPiece piece;
  EXPECT_FALSE(reader.ReadN(10, &piece));  // Not enough buffer.
  EXPECT_TRUE(reader.ReadN(6, &piece));
  EXPECT_TRUE(piece.IsPinned());
  EXPECT_EQ(SpdyStringPiece("foobar"), SpdyStringPiece(piece));
  EXPECT_EQ(0u, reader.Available());
}

TEST_F(SpdyPrefixedBufferReaderTest, ReadMixed) {
  SpdyPrefixedBufferReader reader = Build("abcdef", "hijkl");
  EXPECT_EQ(11u, reader.Available());

  char buffer[] = "1234";
  SpdyPinnableBufferPiece piece;

  EXPECT_TRUE(reader.ReadN(3, buffer));
  EXPECT_THAT(buffer, ElementsAreArray("abc4"));
  EXPECT_EQ(8u, reader.Available());

  EXPECT_TRUE(reader.ReadN(2, buffer));
  EXPECT_THAT(buffer, ElementsAreArray("dec4"));
  EXPECT_EQ(6u, reader.Available());

  EXPECT_TRUE(reader.ReadN(3, &piece));
  EXPECT_EQ(SpdyStringPiece("fhi"), SpdyStringPiece(piece));
  EXPECT_TRUE(piece.IsPinned());
  EXPECT_EQ(3u, reader.Available());

  EXPECT_TRUE(reader.ReadN(2, &piece));
  EXPECT_EQ(SpdyStringPiece("jk"), SpdyStringPiece(piece));
  EXPECT_FALSE(piece.IsPinned());
  EXPECT_EQ(1u, reader.Available());

  EXPECT_TRUE(reader.ReadN(1, buffer));
  EXPECT_THAT(buffer, ElementsAreArray("lec4"));
  EXPECT_EQ(0u, reader.Available());
}

}  // namespace test

}  // namespace spdy
