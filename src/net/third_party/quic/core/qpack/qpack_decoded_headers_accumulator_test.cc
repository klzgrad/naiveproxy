// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_decoded_headers_accumulator.h"

#include <cstring>

#include "net/third_party/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quic/core/qpack/qpack_decoder_test_utils.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"

using ::testing::Eq;
using ::testing::StrictMock;

namespace quic {
namespace test {
namespace {

QuicStreamId kTestStreamId = 1;

// Header Acknowledgement decoder stream instruction with stream_id = 1.
const char* const kHeaderAcknowledgement = "\x81";

}  // anonymous namespace

class QpackDecodedHeadersAccumulatorTest : public QuicTest {
 protected:
  QpackDecodedHeadersAccumulatorTest()
      : qpack_decoder_(&encoder_stream_error_delegate_,
                       &decoder_stream_sender_delegate_),
        accumulator_(kTestStreamId, &qpack_decoder_) {}

  NoopEncoderStreamErrorDelegate encoder_stream_error_delegate_;
  StrictMock<MockDecoderStreamSenderDelegate> decoder_stream_sender_delegate_;
  QpackDecoder qpack_decoder_;
  QpackDecodedHeadersAccumulator accumulator_;
};

// HEADERS frame payload must have a complete Header Block Prefix.
TEST_F(QpackDecodedHeadersAccumulatorTest, EmptyPayload) {
  EXPECT_FALSE(accumulator_.EndHeaderBlock());
  EXPECT_EQ("Incomplete header data prefix.", accumulator_.error_message());
}

// HEADERS frame payload must have a complete Header Block Prefix.
TEST_F(QpackDecodedHeadersAccumulatorTest, TruncatedHeaderBlockPrefix) {
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("00")));
  EXPECT_FALSE(accumulator_.EndHeaderBlock());
  EXPECT_EQ("Incomplete header data prefix.", accumulator_.error_message());
}

TEST_F(QpackDecodedHeadersAccumulatorTest, EmptyHeaderList) {
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteDecoderStreamData(Eq(kHeaderAcknowledgement)));

  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("0000")));
  EXPECT_TRUE(accumulator_.EndHeaderBlock());

  EXPECT_TRUE(accumulator_.quic_header_list().empty());
}

// This payload is the prefix of a valid payload, but EndHeaderBlock() is called
// before it can be completely decoded.
TEST_F(QpackDecodedHeadersAccumulatorTest, TruncatedPayload) {
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("00002366")));
  EXPECT_FALSE(accumulator_.EndHeaderBlock());
  EXPECT_EQ("Incomplete header block.", accumulator_.error_message());
}

// This payload is invalid because it refers to a non-existing static entry.
TEST_F(QpackDecodedHeadersAccumulatorTest, InvalidPayload) {
  EXPECT_FALSE(accumulator_.Decode(QuicTextUtils::HexDecode("0000ff23ff24")));
  EXPECT_EQ("Static table entry not found.", accumulator_.error_message());
}

TEST_F(QpackDecodedHeadersAccumulatorTest, Success) {
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteDecoderStreamData(Eq(kHeaderAcknowledgement)));

  QuicString encoded_data(QuicTextUtils::HexDecode("000023666f6f03626172"));
  EXPECT_TRUE(accumulator_.Decode(encoded_data));
  EXPECT_TRUE(accumulator_.EndHeaderBlock());

  auto header_list = accumulator_.quic_header_list();
  auto it = header_list.begin();
  EXPECT_TRUE(it != header_list.end());
  EXPECT_EQ("foo", it->first);
  EXPECT_EQ("bar", it->second);
  ++it;
  EXPECT_TRUE(it == header_list.end());

  EXPECT_EQ(strlen("foo") + strlen("bar"),
            header_list.uncompressed_header_bytes());
  EXPECT_EQ(encoded_data.size(), header_list.compressed_header_bytes());
}

}  // namespace test
}  // namespace quic
