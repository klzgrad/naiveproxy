// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_encoder.h"

#include "net/third_party/quic/core/qpack/qpack_encoder_test_utils.h"
#include "net/third_party/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::Values;

namespace quic {
namespace test {
namespace {

class QpackEncoderTest : public QuicTestWithParam<FragmentMode> {
 public:
  QpackEncoderTest() : fragment_mode_(GetParam()) {}

  QuicString Encode(const spdy::SpdyHeaderBlock* header_list) {
    return QpackEncode(FragmentModeToFragmentSizeGenerator(fragment_mode_),
                       header_list);
  }

 private:
  const FragmentMode fragment_mode_;
};

INSTANTIATE_TEST_CASE_P(,
                        QpackEncoderTest,
                        Values(FragmentMode::kSingleChunk,
                               FragmentMode::kOctetByOctet));

TEST_P(QpackEncoderTest, Empty) {
  spdy::SpdyHeaderBlock header_list;
  QuicString output = Encode(&header_list);

  EXPECT_TRUE(output.empty());
}

TEST_P(QpackEncoderTest, EmptyName) {
  spdy::SpdyHeaderBlock header_list;
  header_list[""] = "foo";
  QuicString output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("208294e7"), output);
}

TEST_P(QpackEncoderTest, EmptyValue) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "";
  QuicString output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("2a94e700"), output);
}

TEST_P(QpackEncoderTest, EmptyNameAndValue) {
  spdy::SpdyHeaderBlock header_list;
  header_list[""] = "";
  QuicString output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("2000"), output);
}

TEST_P(QpackEncoderTest, Simple) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "bar";
  QuicString output = Encode(&header_list);

  EXPECT_EQ(QuicTextUtils::HexDecode("2a94e703626172"), output);
}

TEST_P(QpackEncoderTest, Multiple) {
  spdy::SpdyHeaderBlock header_list;
  header_list["foo"] = "bar";
  // 'Z' would be Huffman encoded to 8 bits, so no Huffman encoding is used.
  header_list["ZZZZZZZ"] = QuicString(127, 'Z');
  QuicString output = Encode(&header_list);

  EXPECT_EQ(
      QuicTextUtils::HexDecode(
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

TEST_P(QpackEncoderTest, StaticTable) {
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "GET";
    header_list["accept-encoding"] = "gzip, deflate";
    header_list["cache-control"] = "";

    QuicString output = Encode(&header_list);
    EXPECT_EQ(QuicTextUtils::HexDecode("c2d0d8"), output);
  }
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "POST";
    header_list["accept-encoding"] = "brotli";
    header_list["cache-control"] = "foo";

    QuicString output = Encode(&header_list);
    DLOG(INFO) << QuicTextUtils::HexEncode(output);
    EXPECT_EQ(QuicTextUtils::HexDecode("c35f01858ec3a6837f5f098294e7"), output);
  }
  {
    spdy::SpdyHeaderBlock header_list;
    header_list[":method"] = "CONNECT";
    header_list["accept-encoding"] = "";

    QuicString output = Encode(&header_list);
    EXPECT_EQ(QuicTextUtils::HexDecode("5207434f4e4e4543545f0100"), output);
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
