// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_decoded_headers_accumulator.h"

#include <cstring>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "quic/core/qpack/qpack_decoder.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/qpack/qpack_decoder_test_utils.h"
#include "quic/test_tools/qpack/qpack_test_utils.h"
#include "common/platform/api/quiche_text_utils.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Pair;
using ::testing::SaveArg;
using ::testing::StrictMock;

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

class MockVisitor : public QpackDecodedHeadersAccumulator::Visitor {
 public:
  ~MockVisitor() override = default;
  MOCK_METHOD(void,
              OnHeadersDecoded,
              (QuicHeaderList headers, bool header_list_size_limit_exceeded),
              (override));
  MOCK_METHOD(void,
              OnHeaderDecodingError,
              (absl::string_view error_message),
              (override));
};

}  // anonymous namespace

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
  StrictMock<MockVisitor> visitor_;
  QpackDecodedHeadersAccumulator accumulator_;
};

// HEADERS frame payload must have a complete Header Block Prefix.
TEST_F(QpackDecodedHeadersAccumulatorTest, EmptyPayload) {
  EXPECT_CALL(visitor_,
              OnHeaderDecodingError(Eq("Incomplete header data prefix.")));
  accumulator_.EndHeaderBlock();
}

// HEADERS frame payload must have a complete Header Block Prefix.
TEST_F(QpackDecodedHeadersAccumulatorTest, TruncatedHeaderBlockPrefix) {
  accumulator_.Decode(absl::HexStringToBytes("00"));

  EXPECT_CALL(visitor_,
              OnHeaderDecodingError(Eq("Incomplete header data prefix.")));
  accumulator_.EndHeaderBlock();
}

TEST_F(QpackDecodedHeadersAccumulatorTest, EmptyHeaderList) {
  std::string encoded_data(absl::HexStringToBytes("0000"));
  accumulator_.Decode(encoded_data);

  QuicHeaderList header_list;
  EXPECT_CALL(visitor_, OnHeadersDecoded(_, false))
      .WillOnce(SaveArg<0>(&header_list));
  accumulator_.EndHeaderBlock();

  EXPECT_EQ(0u, header_list.uncompressed_header_bytes());
  EXPECT_EQ(encoded_data.size(), header_list.compressed_header_bytes());
  EXPECT_TRUE(header_list.empty());
}

// This payload is the prefix of a valid payload, but EndHeaderBlock() is called
// before it can be completely decoded.
TEST_F(QpackDecodedHeadersAccumulatorTest, TruncatedPayload) {
  accumulator_.Decode(absl::HexStringToBytes("00002366"));

  EXPECT_CALL(visitor_, OnHeaderDecodingError(Eq("Incomplete header block.")));
  accumulator_.EndHeaderBlock();
}

// This payload is invalid because it refers to a non-existing static entry.
TEST_F(QpackDecodedHeadersAccumulatorTest, InvalidPayload) {
  EXPECT_CALL(visitor_,
              OnHeaderDecodingError(Eq("Static table entry not found.")));
  accumulator_.Decode(absl::HexStringToBytes("0000ff23ff24"));
}

TEST_F(QpackDecodedHeadersAccumulatorTest, Success) {
  std::string encoded_data(absl::HexStringToBytes("000023666f6f03626172"));
  accumulator_.Decode(encoded_data);

  QuicHeaderList header_list;
  EXPECT_CALL(visitor_, OnHeadersDecoded(_, false))
      .WillOnce(SaveArg<0>(&header_list));
  accumulator_.EndHeaderBlock();

  EXPECT_THAT(header_list, ElementsAre(Pair("foo", "bar")));
  EXPECT_EQ(strlen("foo") + strlen("bar"),
            header_list.uncompressed_header_bytes());
  EXPECT_EQ(encoded_data.size(), header_list.compressed_header_bytes());
}

// Test that Decode() calls are not ignored after header list limit is exceeded,
// otherwise decoding could fail with "incomplete header block" error.
TEST_F(QpackDecodedHeadersAccumulatorTest, ExceedLimitThenSplitInstruction) {
  // Total length of header list exceeds kMaxHeaderListSize.
  accumulator_.Decode(absl::HexStringToBytes(
      "0000"                                      // header block prefix
      "26666f6f626172"                            // header key: "foobar"
      "7d61616161616161616161616161616161616161"  // header value: 'a' 125 times
      "616161616161616161616161616161616161616161616161616161616161616161616161"
      "616161616161616161616161616161616161616161616161616161616161616161616161"
      "61616161616161616161616161616161616161616161616161616161616161616161"
      "ff"));  // first byte of a two-byte long Indexed Header Field instruction
  accumulator_.Decode(absl::HexStringToBytes(
      "0f"  // second byte of a two-byte long Indexed Header Field instruction
      ));

  EXPECT_CALL(visitor_, OnHeadersDecoded(_, true));
  accumulator_.EndHeaderBlock();
}

// Test that header list limit enforcement works with blocked encoding.
TEST_F(QpackDecodedHeadersAccumulatorTest, ExceedLimitBlocked) {
  // Total length of header list exceeds kMaxHeaderListSize.
  accumulator_.Decode(absl::HexStringToBytes(
      "0200"            // header block prefix
      "80"              // reference to dynamic table entry not yet received
      "26666f6f626172"  // header key: "foobar"
      "7d61616161616161616161616161616161616161"  // header value: 'a' 125 times
      "616161616161616161616161616161616161616161616161616161616161616161616161"
      "616161616161616161616161616161616161616161616161616161616161616161616161"
      "61616161616161616161616161616161616161616161616161616161616161616161"));
  accumulator_.EndHeaderBlock();

  // Set dynamic table capacity.
  qpack_decoder_.OnSetDynamicTableCapacity(kMaxDynamicTableCapacity);
  // Adding dynamic table entry unblocks decoding.
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));

  EXPECT_CALL(visitor_, OnHeadersDecoded(_, true));
  qpack_decoder_.OnInsertWithoutNameReference("foo", "bar");
}

TEST_F(QpackDecodedHeadersAccumulatorTest, BlockedDecoding) {
  // Reference to dynamic table entry not yet received.
  std::string encoded_data(absl::HexStringToBytes("020080"));
  accumulator_.Decode(encoded_data);
  accumulator_.EndHeaderBlock();

  // Set dynamic table capacity.
  qpack_decoder_.OnSetDynamicTableCapacity(kMaxDynamicTableCapacity);
  // Adding dynamic table entry unblocks decoding.
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));

  QuicHeaderList header_list;
  EXPECT_CALL(visitor_, OnHeadersDecoded(_, false))
      .WillOnce(SaveArg<0>(&header_list));
  qpack_decoder_.OnInsertWithoutNameReference("foo", "bar");

  EXPECT_THAT(header_list, ElementsAre(Pair("foo", "bar")));
  EXPECT_EQ(strlen("foo") + strlen("bar"),
            header_list.uncompressed_header_bytes());
  EXPECT_EQ(encoded_data.size(), header_list.compressed_header_bytes());
}

TEST_F(QpackDecodedHeadersAccumulatorTest,
       BlockedDecodingUnblockedBeforeEndOfHeaderBlock) {
  // Reference to dynamic table entry not yet received.
  accumulator_.Decode(absl::HexStringToBytes("020080"));

  // Set dynamic table capacity.
  qpack_decoder_.OnSetDynamicTableCapacity(kMaxDynamicTableCapacity);
  // Adding dynamic table entry unblocks decoding.
  qpack_decoder_.OnInsertWithoutNameReference("foo", "bar");

  // Rest of header block: same entry again.
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));
  accumulator_.Decode(absl::HexStringToBytes("80"));

  QuicHeaderList header_list;
  EXPECT_CALL(visitor_, OnHeadersDecoded(_, false))
      .WillOnce(SaveArg<0>(&header_list));
  accumulator_.EndHeaderBlock();

  EXPECT_THAT(header_list, ElementsAre(Pair("foo", "bar"), Pair("foo", "bar")));
}

// Regression test for https://crbug.com/1024263.
TEST_F(QpackDecodedHeadersAccumulatorTest,
       BlockedDecodingUnblockedAndErrorBeforeEndOfHeaderBlock) {
  // Required Insert Count higher than number of entries causes decoding to be
  // blocked.
  accumulator_.Decode(absl::HexStringToBytes("0200"));
  // Indexed Header Field instruction addressing dynamic table entry with
  // relative index 0, absolute index 0.
  accumulator_.Decode(absl::HexStringToBytes("80"));
  // Relative index larger than or equal to Base is invalid.
  accumulator_.Decode(absl::HexStringToBytes("81"));

  // Set dynamic table capacity.
  qpack_decoder_.OnSetDynamicTableCapacity(kMaxDynamicTableCapacity);

  // Adding dynamic table entry unblocks decoding.  Error is detected.
  EXPECT_CALL(visitor_, OnHeaderDecodingError(Eq("Invalid relative index.")));
  qpack_decoder_.OnInsertWithoutNameReference("foo", "bar");
}

}  // namespace test
}  // namespace quic
