// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoded_headers_accumulator.h"

#include <cstring>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_test_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Pair;
using ::testing::StrictMock;
using Status = quic::QpackDecodedHeadersAccumulator::Status;

namespace quic {
namespace test {
namespace {

// Arbitrary stream ID used for testing.
QuicStreamId kTestStreamId = 1;

// Limit on header list size.
const size_t kMaxHeaderListSize = 100;

// Maximum dynamic table capacity.
const size_t kMaxDynamicTableCapacity = 100;

// Maximum number of blocked streams.
const uint64_t kMaximumBlockedStreams = 1;

// Header Acknowledgement decoder stream instruction with stream_id = 1.
const char* const kHeaderAcknowledgement = "\x81";

}  // anonymous namespace

class NoopVisitor : public QpackDecodedHeadersAccumulator::Visitor {
 public:
  ~NoopVisitor() override = default;
  void OnHeadersDecoded(QuicHeaderList /* headers */) override {}
  void OnHeaderDecodingError() override {}
};

class QpackDecodedHeadersAccumulatorTest : public QuicTest {
 protected:
  QpackDecodedHeadersAccumulatorTest()
      : qpack_decoder_(kMaxDynamicTableCapacity,
                       kMaximumBlockedStreams,
                       &encoder_stream_error_delegate_),
        accumulator_(kTestStreamId,
                     &qpack_decoder_,
                     &visitor_,
                     kMaxHeaderListSize) {
    qpack_decoder_.set_qpack_stream_sender_delegate(
        &decoder_stream_sender_delegate_);
  }

  NoopEncoderStreamErrorDelegate encoder_stream_error_delegate_;
  StrictMock<MockQpackStreamSenderDelegate> decoder_stream_sender_delegate_;
  QpackDecoder qpack_decoder_;
  NoopVisitor visitor_;
  QpackDecodedHeadersAccumulator accumulator_;
};

// HEADERS frame payload must have a complete Header Block Prefix.
TEST_F(QpackDecodedHeadersAccumulatorTest, EmptyPayload) {
  EXPECT_EQ(Status::kError, accumulator_.EndHeaderBlock());
  EXPECT_EQ("Incomplete header data prefix.", accumulator_.error_message());
}

// HEADERS frame payload must have a complete Header Block Prefix.
TEST_F(QpackDecodedHeadersAccumulatorTest, TruncatedHeaderBlockPrefix) {
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("00")));
  EXPECT_EQ(Status::kError, accumulator_.EndHeaderBlock());
  EXPECT_EQ("Incomplete header data prefix.", accumulator_.error_message());
}

TEST_F(QpackDecodedHeadersAccumulatorTest, EmptyHeaderList) {
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("0000")));
  EXPECT_EQ(Status::kSuccess, accumulator_.EndHeaderBlock());

  EXPECT_TRUE(accumulator_.quic_header_list().empty());
}

// This payload is the prefix of a valid payload, but EndHeaderBlock() is called
// before it can be completely decoded.
TEST_F(QpackDecodedHeadersAccumulatorTest, TruncatedPayload) {
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("00002366")));
  EXPECT_EQ(Status::kError, accumulator_.EndHeaderBlock());
  EXPECT_EQ("Incomplete header block.", accumulator_.error_message());
}

// This payload is invalid because it refers to a non-existing static entry.
TEST_F(QpackDecodedHeadersAccumulatorTest, InvalidPayload) {
  EXPECT_FALSE(accumulator_.Decode(QuicTextUtils::HexDecode("0000ff23ff24")));
  EXPECT_EQ("Static table entry not found.", accumulator_.error_message());
}

TEST_F(QpackDecodedHeadersAccumulatorTest, Success) {
  std::string encoded_data(QuicTextUtils::HexDecode("000023666f6f03626172"));
  EXPECT_TRUE(accumulator_.Decode(encoded_data));
  EXPECT_EQ(Status::kSuccess, accumulator_.EndHeaderBlock());

  const QuicHeaderList& header_list = accumulator_.quic_header_list();
  EXPECT_THAT(header_list, ElementsAre(Pair("foo", "bar")));

  EXPECT_EQ(strlen("foo") + strlen("bar"),
            header_list.uncompressed_header_bytes());
  EXPECT_EQ(encoded_data.size(), header_list.compressed_header_bytes());
}

TEST_F(QpackDecodedHeadersAccumulatorTest, ExceedingLimit) {
  // Total length of header list exceeds kMaxHeaderListSize.
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode(
      "0000"                                      // header block prefix
      "26666f6f626172"                            // header key: "foobar"
      "7d61616161616161616161616161616161616161"  // header value: 'a' 125 times
      "616161616161616161616161616161616161616161616161616161616161616161616161"
      "616161616161616161616161616161616161616161616161616161616161616161616161"
      "61616161616161616161616161616161616161616161616161616161616161616161")));
  EXPECT_EQ(Status::kSuccess, accumulator_.EndHeaderBlock());

  // QuicHeaderList signals header list over limit by clearing it.
  EXPECT_TRUE(accumulator_.quic_header_list().empty());
}

TEST_F(QpackDecodedHeadersAccumulatorTest, BlockedDecoding) {
  // Reference to dynamic table entry not yet received.
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("020080")));
  EXPECT_EQ(Status::kBlocked, accumulator_.EndHeaderBlock());

  // Set dynamic table capacity.
  qpack_decoder_.OnSetDynamicTableCapacity(kMaxDynamicTableCapacity);
  // Adding dynamic table entry unblocks decoding.
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));
  qpack_decoder_.OnInsertWithoutNameReference("foo", "bar");

  EXPECT_THAT(accumulator_.quic_header_list(), ElementsAre(Pair("foo", "bar")));
}

TEST_F(QpackDecodedHeadersAccumulatorTest,
       BlockedDecodingUnblockedBeforeEndOfHeaderBlock) {
  // Reference to dynamic table entry not yet received.
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("020080")));

  // Set dynamic table capacity.
  qpack_decoder_.OnSetDynamicTableCapacity(kMaxDynamicTableCapacity);
  // Adding dynamic table entry unblocks decoding.
  qpack_decoder_.OnInsertWithoutNameReference("foo", "bar");

  // Rest of header block: same entry again.
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));
  EXPECT_TRUE(accumulator_.Decode(QuicTextUtils::HexDecode("80")));
  EXPECT_EQ(Status::kSuccess, accumulator_.EndHeaderBlock());

  EXPECT_THAT(accumulator_.quic_header_list(),
              ElementsAre(Pair("foo", "bar"), Pair("foo", "bar")));
}

}  // namespace test
}  // namespace quic
