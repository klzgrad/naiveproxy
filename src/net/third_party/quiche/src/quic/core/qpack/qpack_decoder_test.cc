// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder_test_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quiche/src/spdy/core/spdy_header_block.h"

using ::testing::Eq;
using ::testing::Mock;
using ::testing::Sequence;
using ::testing::StrictMock;
using ::testing::Values;

namespace quic {
namespace test {
namespace {

// Header Acknowledgement decoder stream instruction with stream_id = 1.
const char* const kHeaderAcknowledgement = "\x81";

const uint64_t kMaximumDynamicTableCapacity = 1024;
const uint64_t kMaximumBlockedStreams = 1;

class QpackDecoderTest : public QuicTestWithParam<FragmentMode> {
 protected:
  QpackDecoderTest()
      : qpack_decoder_(kMaximumDynamicTableCapacity,
                       kMaximumBlockedStreams,
                       &encoder_stream_error_delegate_),
        fragment_mode_(GetParam()) {
    qpack_decoder_.set_qpack_stream_sender_delegate(
        &decoder_stream_sender_delegate_);
  }

  ~QpackDecoderTest() override = default;

  void DecodeEncoderStreamData(QuicStringPiece data) {
    qpack_decoder_.encoder_stream_receiver()->Decode(data);
  }

  std::unique_ptr<QpackProgressiveDecoder> CreateProgressiveDecoder(
      QuicStreamId stream_id) {
    return qpack_decoder_.CreateProgressiveDecoder(stream_id, &handler_);
  }

  // Set up |progressive_decoder_|.
  void StartDecoding() {
    progressive_decoder_ = CreateProgressiveDecoder(/* stream_id = */ 1);
  }

  // Pass header block data to QpackProgressiveDecoder::Decode()
  // in fragments dictated by |fragment_mode_|.
  void DecodeData(QuicStringPiece data) {
    auto fragment_size_generator =
        FragmentModeToFragmentSizeGenerator(fragment_mode_);
    while (!data.empty()) {
      size_t fragment_size = std::min(fragment_size_generator(), data.size());
      progressive_decoder_->Decode(data.substr(0, fragment_size));
      data = data.substr(fragment_size);
    }
  }

  // Signal end of header block to QpackProgressiveDecoder.
  void EndDecoding() {
    progressive_decoder_->EndHeaderBlock();
    // |progressive_decoder_| is kept alive so that it can
    // handle callbacks later in case of blocked decoding.
  }

  // Decode an entire header block.
  void DecodeHeaderBlock(QuicStringPiece data) {
    StartDecoding();
    DecodeData(data);
    EndDecoding();
  }

  StrictMock<MockEncoderStreamErrorDelegate> encoder_stream_error_delegate_;
  StrictMock<MockQpackStreamSenderDelegate> decoder_stream_sender_delegate_;
  StrictMock<MockHeadersHandler> handler_;

 private:
  QpackDecoder qpack_decoder_;
  const FragmentMode fragment_mode_;
  std::unique_ptr<QpackProgressiveDecoder> progressive_decoder_;
};

INSTANTIATE_TEST_SUITE_P(,
                         QpackDecoderTest,
                         Values(FragmentMode::kSingleChunk,
                                FragmentMode::kOctetByOctet));

TEST_P(QpackDecoderTest, NoPrefix) {
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Incomplete header data prefix.")));

  // Header Data Prefix is at least two bytes long.
  DecodeHeaderBlock(QuicTextUtils::HexDecode("00"));
}

TEST_P(QpackDecoderTest, EmptyHeaderBlock) {
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode("0000"));
}

TEST_P(QpackDecoderTest, LiteralEntryEmptyName) {
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(""), Eq("foo")));
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode("00002003666f6f"));
}

TEST_P(QpackDecoderTest, LiteralEntryEmptyValue) {
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("")));
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode("000023666f6f00"));
}

TEST_P(QpackDecoderTest, LiteralEntryEmptyNameAndValue) {
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(""), Eq("")));
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode("00002000"));
}

TEST_P(QpackDecoderTest, SimpleLiteralEntry) {
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode("000023666f6f03626172"));
}

TEST_P(QpackDecoderTest, MultipleLiteralEntries) {
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  std::string str(127, 'a');
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foobaar"), QuicStringPiece(str)));
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0000"                // prefix
      "23666f6f03626172"    // foo: bar
      "2700666f6f62616172"  // 7 octet long header name, the smallest number
                            // that does not fit on a 3-bit prefix.
      "7f0061616161616161"  // 127 octet long header value, the smallest number
      "616161616161616161"  // that does not fit on a 7-bit prefix.
      "6161616161616161616161616161616161616161616161616161616161616161616161"
      "6161616161616161616161616161616161616161616161616161616161616161616161"
      "6161616161616161616161616161616161616161616161616161616161616161616161"
      "616161616161"));
}

// Name Length value is too large for varint decoder to decode.
TEST_P(QpackDecoderTest, NameLenTooLargeForVarintDecoder) {
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Encoded integer too large.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode("000027ffffffffffffffffffff"));
}

// Name Length value can be decoded by varint decoder but exceeds 1 MB limit.
TEST_P(QpackDecoderTest, NameLenExceedsLimit) {
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("String literal too long.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode("000027ffff7f"));
}

// Value Length value is too large for varint decoder to decode.
TEST_P(QpackDecoderTest, ValueLenTooLargeForVarintDecoder) {
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Encoded integer too large.")));

  DecodeHeaderBlock(
      QuicTextUtils::HexDecode("000023666f6f7fffffffffffffffffffff"));
}

// Value Length value can be decoded by varint decoder but exceeds 1 MB limit.
TEST_P(QpackDecoderTest, ValueLenExceedsLimit) {
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("String literal too long.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode("000023666f6f7fffff7f"));
}

TEST_P(QpackDecoderTest, IncompleteHeaderBlock) {
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Incomplete header block.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode("00002366"));
}

TEST_P(QpackDecoderTest, HuffmanSimple) {
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("custom-key"), Eq("custom-value")));
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(
      QuicTextUtils::HexDecode("00002f0125a849e95ba97d7f8925a849e95bb8e8b4bf"));
}

TEST_P(QpackDecoderTest, AlternatingHuffmanNonHuffman) {
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("custom-key"), Eq("custom-value")))
      .Times(4);
  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0000"                        // Prefix.
      "2f0125a849e95ba97d7f"        // Huffman-encoded name.
      "8925a849e95bb8e8b4bf"        // Huffman-encoded value.
      "2703637573746f6d2d6b6579"    // Non-Huffman encoded name.
      "0c637573746f6d2d76616c7565"  // Non-Huffman encoded value.
      "2f0125a849e95ba97d7f"        // Huffman-encoded name.
      "0c637573746f6d2d76616c7565"  // Non-Huffman encoded value.
      "2703637573746f6d2d6b6579"    // Non-Huffman encoded name.
      "8925a849e95bb8e8b4bf"));     // Huffman-encoded value.
}

TEST_P(QpackDecoderTest, HuffmanNameDoesNotHaveEOSPrefix) {
  EXPECT_CALL(handler_, OnDecodingErrorDetected(QuicStringPiece(
                            "Error in Huffman-encoded string.")));

  // 'y' ends in 0b0 on the most significant bit of the last byte.
  // The remaining 7 bits must be a prefix of EOS, which is all 1s.
  DecodeHeaderBlock(
      QuicTextUtils::HexDecode("00002f0125a849e95ba97d7e8925a849e95bb8e8b4bf"));
}

TEST_P(QpackDecoderTest, HuffmanValueDoesNotHaveEOSPrefix) {
  EXPECT_CALL(handler_, OnDecodingErrorDetected(QuicStringPiece(
                            "Error in Huffman-encoded string.")));

  // 'e' ends in 0b101, taking up the 3 most significant bits of the last byte.
  // The remaining 5 bits must be a prefix of EOS, which is all 1s.
  DecodeHeaderBlock(
      QuicTextUtils::HexDecode("00002f0125a849e95ba97d7f8925a849e95bb8e8b4be"));
}

TEST_P(QpackDecoderTest, HuffmanNameEOSPrefixTooLong) {
  EXPECT_CALL(handler_, OnDecodingErrorDetected(QuicStringPiece(
                            "Error in Huffman-encoded string.")));

  // The trailing EOS prefix must be at most 7 bits long.  Appending one octet
  // with value 0xff is invalid, even though 0b111111111111111 (15 bits) is a
  // prefix of EOS.
  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "00002f0225a849e95ba97d7fff8925a849e95bb8e8b4bf"));
}

TEST_P(QpackDecoderTest, HuffmanValueEOSPrefixTooLong) {
  EXPECT_CALL(handler_, OnDecodingErrorDetected(QuicStringPiece(
                            "Error in Huffman-encoded string.")));

  // The trailing EOS prefix must be at most 7 bits long.  Appending one octet
  // with value 0xff is invalid, even though 0b1111111111111 (13 bits) is a
  // prefix of EOS.
  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "00002f0125a849e95ba97d7f8a25a849e95bb8e8b4bfff"));
}

TEST_P(QpackDecoderTest, StaticTable) {
  // A header name that has multiple entries with different values.
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("GET")));
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("POST")));
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("TRACE")));

  // A header name that has a single entry with non-empty value.
  EXPECT_CALL(handler_,
              OnHeaderDecoded(Eq("accept-encoding"), Eq("gzip, deflate, br")));
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("accept-encoding"), Eq("compress")));
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("accept-encoding"), Eq("")));

  // A header name that has a single entry with empty value.
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("location"), Eq("")));
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("location"), Eq("foo")));

  EXPECT_CALL(handler_, OnDecodingCompleted());

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0000d1dfccd45f108621e9aec2a11f5c8294e75f000554524143455f1000"));
}

TEST_P(QpackDecoderTest, TooHighStaticTableIndex) {
  // This is the last entry in the static table with index 98.
  EXPECT_CALL(handler_,
              OnHeaderDecoded(Eq("x-frame-options"), Eq("sameorigin")));

  // Addressing entry 99 should trigger an error.
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Static table entry not found.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode("0000ff23ff24"));
}

TEST_P(QpackDecoderTest, DynamicTable) {
  DecodeEncoderStreamData(QuicTextUtils::HexDecode(
      "3fe107"          // Set dynamic table capacity to 1024.
      "6294e703626172"  // Add literal entry with name "foo" and value "bar".
      "80035a5a5a"      // Add entry with name of dynamic table entry index 0
                        // (relative index) and value "ZZZ".
      "cf8294e7"        // Add entry with name of static table entry index 15
                        // and value "foo".
      "01"));           // Duplicate entry with relative index 1.

  // Now there are four entries in the dynamic table.
  // Entry 0: "foo", "bar"
  // Entry 1: "foo", "ZZZ"
  // Entry 2: ":method", "foo"
  // Entry 3: "foo", "ZZZ"

  // Use a Sequence to test that mock methods are called in order.
  Sequence s;

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("ZZZ"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("foo")))
      .InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("ZZZ"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("ZZ"))).InSequence(s);
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)))
      .InSequence(s);
  EXPECT_CALL(handler_, OnDecodingCompleted()).InSequence(s);

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0500"  // Required Insert Count 4 and Delta Base 0.
              // Base is 4 + 0 = 4.
      "83"    // Dynamic table entry with relative index 3, absolute index 0.
      "82"    // Dynamic table entry with relative index 2, absolute index 1.
      "81"    // Dynamic table entry with relative index 1, absolute index 2.
      "80"    // Dynamic table entry with relative index 0, absolute index 3.
      "41025a5a"));  // Name of entry 1 (relative index) from dynamic table,
                     // with value "ZZ".

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("ZZZ"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("foo")))
      .InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("ZZZ"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("ZZ"))).InSequence(s);
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)))
      .InSequence(s);
  EXPECT_CALL(handler_, OnDecodingCompleted()).InSequence(s);

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0502"  // Required Insert Count 4 and Delta Base 2.
              // Base is 4 + 2 = 6.
      "85"    // Dynamic table entry with relative index 5, absolute index 0.
      "84"    // Dynamic table entry with relative index 4, absolute index 1.
      "83"    // Dynamic table entry with relative index 3, absolute index 2.
      "82"    // Dynamic table entry with relative index 2, absolute index 3.
      "43025a5a"));  // Name of entry 3 (relative index) from dynamic table,
                     // with value "ZZ".

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("ZZZ"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("foo")))
      .InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("ZZZ"))).InSequence(s);
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("ZZ"))).InSequence(s);
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)))
      .InSequence(s);
  EXPECT_CALL(handler_, OnDecodingCompleted()).InSequence(s);

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0582"  // Required Insert Count 4 and Delta Base 2 with sign bit set.
              // Base is 4 - 2 - 1 = 1.
      "80"    // Dynamic table entry with relative index 0, absolute index 0.
      "10"    // Dynamic table entry with post-base index 0, absolute index 1.
      "11"    // Dynamic table entry with post-base index 1, absolute index 2.
      "12"    // Dynamic table entry with post-base index 2, absolute index 3.
      "01025a5a"));  // Name of entry 1 (post-base index) from dynamic table,
                     // with value "ZZ".
}

TEST_P(QpackDecoderTest, DecreasingDynamicTableCapacityEvictsEntries) {
  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  EXPECT_CALL(handler_, OnDecodingCompleted());
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"   // Required Insert Count 1 and Delta Base 0.
               // Base is 1 + 0 = 1.
      "80"));  // Dynamic table entry with relative index 0, absolute index 0.

  // Change dynamic table capacity to 32 bytes, smaller than the entry.
  // This must cause the entry to be evicted.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3f01"));

  EXPECT_CALL(handler_, OnDecodingErrorDetected(
                            Eq("Dynamic table entry already evicted.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"   // Required Insert Count 1 and Delta Base 0.
               // Base is 1 + 0 = 1.
      "80"));  // Dynamic table entry with relative index 0, absolute index 0.
}

TEST_P(QpackDecoderTest, EncoderStreamErrorEntryTooLarge) {
  EXPECT_CALL(encoder_stream_error_delegate_,
              OnEncoderStreamError(Eq("Error inserting literal entry.")));

  // Set dynamic table capacity to 34.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3f03"));
  // Add literal entry with name "foo" and value "bar", size is 32 + 3 + 3 = 38.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));
}

TEST_P(QpackDecoderTest, EncoderStreamErrorInvalidStaticTableEntry) {
  EXPECT_CALL(encoder_stream_error_delegate_,
              OnEncoderStreamError(Eq("Invalid static table entry.")));

  // Address invalid static table entry index 99.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("ff2400"));
}

TEST_P(QpackDecoderTest, EncoderStreamErrorInvalidDynamicTableEntry) {
  EXPECT_CALL(encoder_stream_error_delegate_,
              OnEncoderStreamError(Eq("Invalid relative index.")));

  DecodeEncoderStreamData(QuicTextUtils::HexDecode(
      "3fe107"          // Set dynamic table capacity to 1024.
      "6294e703626172"  // Add literal entry with name "foo" and value "bar".
      "8100"));  // Address dynamic table entry with relative index 1.  Such
                 // entry does not exist.  The most recently added and only
                 // dynamic table entry has relative index 0.
}

TEST_P(QpackDecoderTest, EncoderStreamErrorDuplicateInvalidEntry) {
  EXPECT_CALL(encoder_stream_error_delegate_,
              OnEncoderStreamError(Eq("Invalid relative index.")));

  DecodeEncoderStreamData(QuicTextUtils::HexDecode(
      "3fe107"          // Set dynamic table capacity to 1024.
      "6294e703626172"  // Add literal entry with name "foo" and value "bar".
      "01"));  // Duplicate dynamic table entry with relative index 1.  Such
               // entry does not exist.  The most recently added and only
               // dynamic table entry has relative index 0.
}

TEST_P(QpackDecoderTest, EncoderStreamErrorTooLargeInteger) {
  EXPECT_CALL(encoder_stream_error_delegate_,
              OnEncoderStreamError(Eq("Encoded integer too large.")));

  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fffffffffffffffffffff"));
}

TEST_P(QpackDecoderTest, InvalidDynamicEntryWhenBaseIsZero) {
  EXPECT_CALL(handler_, OnDecodingErrorDetected(Eq("Invalid relative index.")));

  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0280"   // Required Insert Count is 1.  Base 1 - 1 - 0 = 0 is explicitly
               // permitted by the spec.
      "80"));  // However, addressing entry with relative index 0 would point to
               // absolute index -1, which is invalid.
}

TEST_P(QpackDecoderTest, InvalidNegativeBase) {
  EXPECT_CALL(handler_, OnDecodingErrorDetected(Eq("Error calculating Base.")));

  // Required Insert Count 1, Delta Base 1 with sign bit set, Base would
  // be 1 - 1 - 1 = -1, but it is not allowed to be negative.
  DecodeHeaderBlock(QuicTextUtils::HexDecode("0281"));
}

TEST_P(QpackDecoderTest, InvalidDynamicEntryByRelativeIndex) {
  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));

  EXPECT_CALL(handler_, OnDecodingErrorDetected(Eq("Invalid relative index.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"   // Required Insert Count 1 and Delta Base 0.
               // Base is 1 + 0 = 1.
      "81"));  // Indexed Header Field instruction addressing relative index 1.
               // This is absolute index -1, which is invalid.

  EXPECT_CALL(handler_, OnDecodingErrorDetected(Eq("Invalid relative index.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"     // Required Insert Count 1 and Delta Base 0.
                 // Base is 1 + 0 = 1.
      "4100"));  // Literal Header Field with Name Reference instruction
                 // addressing relative index 1.  This is absolute index -1,
                 // which is invalid.
}

TEST_P(QpackDecoderTest, EvictedDynamicTableEntry) {
  // Update dynamic table capacity to 128.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3f61"));

  // Add literal entry with name "foo" and value "bar", size 32 + 3 + 3 = 38.
  // This fits in the table three times.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));
  // Duplicate entry four times.  This evicts the first two instances.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("00000000"));

  EXPECT_CALL(handler_, OnDecodingErrorDetected(
                            Eq("Dynamic table entry already evicted.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0500"   // Required Insert Count 4 and Delta Base 0.
               // Base is 4 + 0 = 4.
      "82"));  // Indexed Header Field instruction addressing relative index 2.
               // This is absolute index 1. Such entry does not exist.

  EXPECT_CALL(handler_, OnDecodingErrorDetected(
                            Eq("Dynamic table entry already evicted.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0500"     // Required Insert Count 4 and Delta Base 0.
                 // Base is 4 + 0 = 4.
      "4200"));  // Literal Header Field with Name Reference instruction
                 // addressing relative index 2.  This is absolute index 1. Such
                 // entry does not exist.

  EXPECT_CALL(handler_, OnDecodingErrorDetected(
                            Eq("Dynamic table entry already evicted.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0380"   // Required Insert Count 2 and Delta Base 0 with sign bit set.
               // Base is 2 - 0 - 1 = 1
      "10"));  // Indexed Header Field instruction addressing dynamic table
               // entry with post-base index 0, absolute index 1.  Such entry
               // does not exist.

  EXPECT_CALL(handler_, OnDecodingErrorDetected(
                            Eq("Dynamic table entry already evicted.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0380"     // Required Insert Count 2 and Delta Base 0 with sign bit set.
                 // Base is 2 - 0 - 1 = 1
      "0000"));  // Literal Header Field With Name Reference instruction
                 // addressing dynamic table entry with post-base index 0,
                 // absolute index 1.  Such entry does not exist.
}

TEST_P(QpackDecoderTest, TableCapacityMustNotExceedMaximum) {
  EXPECT_CALL(
      encoder_stream_error_delegate_,
      OnEncoderStreamError(Eq("Error updating dynamic table capacity.")));

  // Try to update dynamic table capacity to 2048, which exceeds the maximum.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe10f"));
}

TEST_P(QpackDecoderTest, SetDynamicTableCapacity) {
  // Update dynamic table capacity to 128, which does not exceed the maximum.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3f61"));
}

TEST_P(QpackDecoderTest, InvalidEncodedRequiredInsertCount) {
  // Maximum dynamic table capacity is 1024.
  // MaxEntries is 1024 / 32 = 32.
  // Required Insert Count is decoded modulo 2 * MaxEntries, that is, modulo 64.
  // A value of 1 cannot be encoded as 65 even though it has the same remainder.
  EXPECT_CALL(handler_, OnDecodingErrorDetected(
                            Eq("Error decoding Required Insert Count.")));
  DecodeHeaderBlock(QuicTextUtils::HexDecode("4100"));
}

// Regression test for https://crbug.com/970218:  Decoder must stop processing
// after a Header Block Prefix with an invalid Encoded Required Insert Count.
TEST_P(QpackDecoderTest, DataAfterInvalidEncodedRequiredInsertCount) {
  EXPECT_CALL(handler_, OnDecodingErrorDetected(
                            Eq("Error decoding Required Insert Count.")));
  // Header Block Prefix followed by some extra data.
  DecodeHeaderBlock(QuicTextUtils::HexDecode("410000"));
}

TEST_P(QpackDecoderTest, WrappedRequiredInsertCount) {
  // Maximum dynamic table capacity is 1024.
  // MaxEntries is 1024 / 32 = 32.

  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and a 600 byte long value.  This will fit
  // in the dynamic table once but not twice.
  DecodeEncoderStreamData(
      QuicTextUtils::HexDecode("6294e7"     // Name "foo".
                               "7fd903"));  // Value length 600.
  std::string header_value(600, 'Z');
  DecodeEncoderStreamData(header_value);

  // Duplicate most recent entry 200 times.
  DecodeEncoderStreamData(std::string(200, '\x00'));

  // Now there is only one entry in the dynamic table, with absolute index 200.

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq(header_value)));
  EXPECT_CALL(handler_, OnDecodingCompleted());
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));

  // Send header block with Required Insert Count = 201.
  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0a00"   // Encoded Required Insert Count 10, Required Insert Count 201,
               // Delta Base 0, Base 201.
      "80"));  // Emit dynamic table entry with relative index 0.
}

TEST_P(QpackDecoderTest, NonZeroRequiredInsertCountButNoDynamicEntries) {
  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("GET")));
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Required Insert Count too large.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"   // Required Insert Count is 1.
      "d1"));  // But the only instruction references the static table.
}

TEST_P(QpackDecoderTest, AddressEntryNotAllowedByRequiredInsertCount) {
  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));

  EXPECT_CALL(
      handler_,
      OnDecodingErrorDetected(
          Eq("Absolute Index must be smaller than Required Insert Count.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0201"   // Required Insert Count 1 and Delta Base 1.
               // Base is 1 + 1 = 2.
      "80"));  // Indexed Header Field instruction addressing dynamic table
               // entry with relative index 0, absolute index 1.  This is not
               // allowed by Required Insert Count.

  EXPECT_CALL(
      handler_,
      OnDecodingErrorDetected(
          Eq("Absolute Index must be smaller than Required Insert Count.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0201"     // Required Insert Count 1 and Delta Base 1.
                 // Base is 1 + 1 = 2.
      "4000"));  // Literal Header Field with Name Reference instruction
                 // addressing dynamic table entry with relative index 0,
                 // absolute index 1.  This is not allowed by Required Index
                 // Count.

  EXPECT_CALL(
      handler_,
      OnDecodingErrorDetected(
          Eq("Absolute Index must be smaller than Required Insert Count.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"   // Required Insert Count 1 and Delta Base 0.
               // Base is 1 + 0 = 1.
      "10"));  // Indexed Header Field with Post-Base Index instruction
               // addressing dynamic table entry with post-base index 0,
               // absolute index 1.  This is not allowed by Required Insert
               // Count.

  EXPECT_CALL(
      handler_,
      OnDecodingErrorDetected(
          Eq("Absolute Index must be smaller than Required Insert Count.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"     // Required Insert Count 1 and Delta Base 0.
                 // Base is 1 + 0 = 1.
      "0000"));  // Literal Header Field with Post-Base Name Reference
                 // instruction addressing dynamic table entry with post-base
                 // index 0, absolute index 1.  This is not allowed by Required
                 // Index Count.
}

TEST_P(QpackDecoderTest, PromisedRequiredInsertCountLargerThanActual) {
  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));
  // Duplicate entry twice so that decoding of header blocks with Required
  // Insert Count not exceeding 3 is not blocked.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("00"));
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("00"));

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Required Insert Count too large.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0300"   // Required Insert Count 2 and Delta Base 0.
               // Base is 2 + 0 = 2.
      "81"));  // Indexed Header Field instruction addressing dynamic table
               // entry with relative index 1, absolute index 0.  Header block
               // requires insert count of 1, even though Required Insert Count
               // is 2.

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("")));
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Required Insert Count too large.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0300"     // Required Insert Count 2 and Delta Base 0.
                 // Base is 2 + 0 = 2.
      "4100"));  // Literal Header Field with Name Reference instruction
                 // addressing dynamic table entry with relative index 1,
                 // absolute index 0.  Header block requires insert count of 1,
                 // even though Required Insert Count is 2.

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Required Insert Count too large.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0481"   // Required Insert Count 3 and Delta Base 1 with sign bit set.
               // Base is 3 - 1 - 1 = 1.
      "10"));  // Indexed Header Field with Post-Base Index instruction
               // addressing dynamic table entry with post-base index 0,
               // absolute index 1.  Header block requires insert count of 2,
               // even though Required Insert Count is 3.

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("")));
  EXPECT_CALL(handler_,
              OnDecodingErrorDetected(Eq("Required Insert Count too large.")));

  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0481"     // Required Insert Count 3 and Delta Base 1 with sign bit set.
                 // Base is 3 - 1 - 1 = 1.
      "0000"));  // Literal Header Field with Post-Base Name Reference
                 // instruction addressing dynamic table entry with post-base
                 // index 0, absolute index 1.  Header block requires insert
                 // count of 2, even though Required Insert Count is 3.
}

TEST_P(QpackDecoderTest, BlockedDecoding) {
  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0200"   // Required Insert Count 1 and Delta Base 0.
               // Base is 1 + 0 = 1.
      "80"));  // Indexed Header Field instruction addressing dynamic table
               // entry with relative index 0, absolute index 0.

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  EXPECT_CALL(handler_, OnDecodingCompleted());
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));

  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));
}

TEST_P(QpackDecoderTest, BlockedDecodingUnblockedBeforeEndOfHeaderBlock) {
  StartDecoding();
  DecodeData(QuicTextUtils::HexDecode(
      "0200"   // Required Insert Count 1 and Delta Base 0.
               // Base is 1 + 0 = 1.
      "80"     // Indexed Header Field instruction addressing dynamic table
               // entry with relative index 0, absolute index 0.
      "d1"));  // Static table entry with index 17.

  // Add literal entry with name "foo" and value "bar".  Decoding is now
  // unblocked because dynamic table Insert Count reached the Required Insert
  // Count of the header block.  |handler_| methods are called immediately for
  // the already consumed part of the header block.
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":method"), Eq("GET")));

  // Set dynamic table capacity to 1024.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3fe107"));
  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));
  Mock::VerifyAndClearExpectations(&handler_);

  // Rest of header block is processed by QpackProgressiveDecoder
  // in the unblocked state.
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("bar")));
  EXPECT_CALL(handler_, OnHeaderDecoded(Eq(":scheme"), Eq("https")));
  DecodeData(QuicTextUtils::HexDecode(
      "80"     // Indexed Header Field instruction addressing dynamic table
               // entry with relative index 0, absolute index 0.
      "d7"));  // Static table entry with index 23.
  Mock::VerifyAndClearExpectations(&handler_);

  EXPECT_CALL(handler_, OnDecodingCompleted());
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));
  EndDecoding();
}

// Make sure that Required Insert Count is compared to Insert Count,
// not size of dynamic table.
TEST_P(QpackDecoderTest, BlockedDecodingAndEvictedEntries) {
  // Update dynamic table capacity to 128.
  // At most three non-empty entries fit in the dynamic table.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("3f61"));

  StartDecoding();
  DecodeHeaderBlock(QuicTextUtils::HexDecode(
      "0700"   // Required Insert Count 6 and Delta Base 0.
               // Base is 6 + 0 = 6.
      "80"));  // Indexed Header Field instruction addressing dynamic table
               // entry with relative index 0, absolute index 5.

  // Add literal entry with name "foo" and value "bar".
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e703626172"));

  // Duplicate entry four times.  This evicts the first two instances.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("00000000"));

  EXPECT_CALL(handler_, OnHeaderDecoded(Eq("foo"), Eq("baz")));
  EXPECT_CALL(handler_, OnDecodingCompleted());
  EXPECT_CALL(decoder_stream_sender_delegate_,
              WriteStreamData(Eq(kHeaderAcknowledgement)));

  // Add literal entry with name "foo" and value "bar".
  // Insert Count is now 6, reaching Required Insert Count of the header block.
  DecodeEncoderStreamData(QuicTextUtils::HexDecode("6294e70362617a"));
}

TEST_P(QpackDecoderTest, TooManyBlockedStreams) {
  // Required Insert Count 1 and Delta Base 0.
  // Without any dynamic table entries received, decoding is blocked.
  std::string data = QuicTextUtils::HexDecode("0200");

  auto progressive_decoder1 = CreateProgressiveDecoder(/* stream_id = */ 1);
  progressive_decoder1->Decode(data);

  EXPECT_CALL(handler_, OnDecodingErrorDetected(Eq(
                            "Limit on number of blocked streams exceeded.")));

  auto progressive_decoder2 = CreateProgressiveDecoder(/* stream_id = */ 2);
  progressive_decoder2->Decode(data);
}

}  // namespace
}  // namespace test
}  // namespace quic
