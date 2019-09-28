// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_test_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

using ::testing::Eq;
using ::testing::StrictMock;
using ::testing::Values;

namespace quic {
namespace test {
namespace {

class QpackEncoderTest : public QuicTest {
 protected:
  QpackEncoderTest() = default;
  ~QpackEncoderTest() override = default;

  std::string Encode(const spdy::SpdyHeaderBlock* header_list) {
    QpackEncoder encoder(&decoder_stream_error_delegate_,
                         &encoder_stream_sender_delegate_);
    return encoder.EncodeHeaderList(/* stream_id = */ 1, header_list);
  }

  StrictMock<MockDecoderStreamErrorDelegate> decoder_stream_error_delegate_;
  NoopQpackStreamSenderDelegate encoder_stream_sender_delegate_;
};

TEST_F(QpackEncoderTest, Empty) {
  spdy::SpdyHeaderBlock header_list;
  std::string output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("0000"), output);
}

TEST_F(QpackEncoderTest, EmptyName) {
  spdy::SpdyHeaderBlock header_list;
  header_list[""] = "foo";
  std::string output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("0000208294e7"), output);
}

TEST_F(QpackEncoderTest, EmptyValue) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "";
  std::string output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("00002a94e700"), output);
}

TEST_F(QpackEncoderTest, EmptyNameAndValue) {
  spdy::SpdyHeaderBlock header_list;
  header_list[""] = "";
  std::string output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("00002000"), output);
}

TEST_F(QpackEncoderTest, Simple) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "bar";
  std::string output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("00002a94e703626172"), output);
}

TEST_F(QpackEncoderTest, Multiple) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "bar";
  // 'Z' would be Huffman encoded to 8 bits, so no Huffman encoding is used.
  header_list["ZZZZZZZ"] = std::string(127, 'Z');
  std::string output = Encode(&header_list);

  EXPECT_EQ(
      QuicTextUtils::HexDecode(
          "0000"                // prefix
          "2a94e703626172"      // foo: bar
          "27005a5a5a5a5a5a5a"  // 7 octet long header name, the smallest number
                                // that does not fit on a 3-bit prefix.
          "7f005a5a5a5a5a5a5a"  // 127 octet long header value, the smallest
          "5a5a5a5a5a5a5a5a5a"  // number that does not fit on a 7-bit prefix.
          "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
          "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
          "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
          "5a5a5a5a5a5a5a5a5a"),
      output);
}

TEST_F(QpackEncoderTest, StaticTable) {
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "GET";
    header_list["accept-encoding"] = "gzip, deflate, br";
    header_list["location"] = "";

    std::string output = Encode(&header_list);
    EXPECT_EQ(QuicTextUtils::HexDecode("0000d1dfcc"), output);
  }
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "POST";
    header_list["accept-encoding"] = "compress";
    header_list["location"] = "foo";

    std::string output = Encode(&header_list);
    EXPECT_EQ(QuicTextUtils::HexDecode("0000d45f108621e9aec2a11f5c8294e7"),
              output);
  }
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "TRACE";
    header_list["accept-encoding"] = "";

    std::string output = Encode(&header_list);
    EXPECT_EQ(QuicTextUtils::HexDecode("00005f000554524143455f1000"), output);
  }
}

TEST_F(QpackEncoderTest, DecoderStreamError) {
  EXPECT_CALL(decoder_stream_error_delegate_,
              OnDecoderStreamError(Eq("Encoded integer too large.")));

  QpackEncoder encoder(&decoder_stream_error_delegate_,
                       &encoder_stream_sender_delegate_);
  encoder.DecodeDecoderStreamData(
      QuicTextUtils::HexDecode("ffffffffffffffffffffff"));
}

TEST_F(QpackEncoderTest, SplitAlongNullCharacter) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = QuicStringPiece("bar\0bar\0baz", 11);
  std::string output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("0000"            // prefix
                                     "2a94e703626172"  // foo: bar
                                     "2a94e703626172"  // foo: bar
                                     "2a94e70362617a"  // foo: baz
                                     ),
            output);
}

}  // namespace
}  // namespace test
}  // namespace quic
