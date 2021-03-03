// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spdy/core/spdy_pinnable_buffer_piece.h"

#include <string>

#include "common/platform/api/quiche_test.h"
#include "spdy/core/spdy_prefixed_buffer_reader.h"

namespace spdy {

namespace test {

class SpdyPinnableBufferPieceTest : public QuicheTest {
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

TEST_F(SpdyPinnableBufferPieceTest, Pin) {
  SpdyPrefixedBufferReader reader = Build("foobar", "");
  SpdyPinnableBufferPiece piece;
  EXPECT_TRUE(reader.ReadN(6, &piece));

  // Piece points to underlying prefix storage.
  EXPECT_EQ(absl::string_view("foobar"), absl::string_view(piece));
  EXPECT_FALSE(piece.IsPinned());
  EXPECT_EQ(prefix_.data(), piece.buffer());

  piece.Pin();

  // Piece now points to allocated storage.
  EXPECT_EQ(absl::string_view("foobar"), absl::string_view(piece));
  EXPECT_TRUE(piece.IsPinned());
  EXPECT_NE(prefix_.data(), piece.buffer());

  // Pinning again has no effect.
  const char* buffer = piece.buffer();
  piece.Pin();
  EXPECT_EQ(buffer, piece.buffer());
}

TEST_F(SpdyPinnableBufferPieceTest, Swap) {
  SpdyPrefixedBufferReader reader = Build("foobar", "");
  SpdyPinnableBufferPiece piece1, piece2;
  EXPECT_TRUE(reader.ReadN(4, &piece1));
  EXPECT_TRUE(reader.ReadN(2, &piece2));

  piece1.Pin();

  EXPECT_EQ(absl::string_view("foob"), absl::string_view(piece1));
  EXPECT_TRUE(piece1.IsPinned());
  EXPECT_EQ(absl::string_view("ar"), absl::string_view(piece2));
  EXPECT_FALSE(piece2.IsPinned());

  piece1.Swap(&piece2);

  EXPECT_EQ(absl::string_view("ar"), absl::string_view(piece1));
  EXPECT_FALSE(piece1.IsPinned());
  EXPECT_EQ(absl::string_view("foob"), absl::string_view(piece2));
  EXPECT_TRUE(piece2.IsPinned());

  SpdyPinnableBufferPiece empty;
  piece2.Swap(&empty);

  EXPECT_EQ(absl::string_view(""), absl::string_view(piece2));
  EXPECT_FALSE(piece2.IsPinned());
}

}  // namespace test

}  // namespace spdy
