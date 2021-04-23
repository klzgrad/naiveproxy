// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_encoder_stream_receiver.h"

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_test.h"
#include "common/platform/api/quiche_text_utils.h"

using testing::Eq;
using testing::StrictMock;

namespace quic {
namespace test {
namespace {

class MockDelegate : public QpackEncoderStreamReceiver::Delegate {
 public:
  ~MockDelegate() override = default;

  MOCK_METHOD(void,
              OnInsertWithNameReference,
              (bool is_static, uint64_t name_index, absl::string_view value),
              (override));
  MOCK_METHOD(void,
              OnInsertWithoutNameReference,
              (absl::string_view name, absl::string_view value),
              (override));
  MOCK_METHOD(void, OnDuplicate, (uint64_t index), (override));
  MOCK_METHOD(void, OnSetDynamicTableCapacity, (uint64_t capacity), (override));
  MOCK_METHOD(void,
              OnErrorDetected,
              (QuicErrorCode error_code, absl::string_view error_message),
              (override));
};

class QpackEncoderStreamReceiverTest : public QuicTest {
 protected:
  QpackEncoderStreamReceiverTest() : stream_(&delegate_) {}
  ~QpackEncoderStreamReceiverTest() override = default;

  void Decode(absl::string_view data) { stream_.Decode(data); }
  StrictMock<MockDelegate>* delegate() { return &delegate_; }

 private:
  QpackEncoderStreamReceiver stream_;
  StrictMock<MockDelegate> delegate_;
};

TEST_F(QpackEncoderStreamReceiverTest, InsertWithNameReference) {
  // Static, index fits in prefix, empty value.
  EXPECT_CALL(*delegate(), OnInsertWithNameReference(true, 5, Eq("")));
  // Static, index fits in prefix, Huffman encoded value.
  EXPECT_CALL(*delegate(), OnInsertWithNameReference(true, 2, Eq("foo")));
  // Not static, index does not fit in prefix, not Huffman encoded value.
  EXPECT_CALL(*delegate(), OnInsertWithNameReference(false, 137, Eq("bar")));
  // Value length does not fit in prefix.
  // 'Z' would be Huffman encoded to 8 bits, so no Huffman encoding is used.
  EXPECT_CALL(*delegate(),
              OnInsertWithNameReference(false, 42, Eq(std::string(127, 'Z'))));

  Decode(absl::HexStringToBytes(
      "c500"
      "c28294e7"
      "bf4a03626172"
      "aa7f005a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"));
}

TEST_F(QpackEncoderStreamReceiverTest, InsertWithNameReferenceIndexTooLarge) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));

  Decode(absl::HexStringToBytes("bfffffffffffffffffffffff"));
}

TEST_F(QpackEncoderStreamReceiverTest, InsertWithNameReferenceValueTooLong) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));

  Decode(absl::HexStringToBytes("c57fffffffffffffffffffff"));
}

TEST_F(QpackEncoderStreamReceiverTest, InsertWithoutNameReference) {
  // Empty name and value.
  EXPECT_CALL(*delegate(), OnInsertWithoutNameReference(Eq(""), Eq("")));
  // Huffman encoded short strings.
  EXPECT_CALL(*delegate(), OnInsertWithoutNameReference(Eq("bar"), Eq("bar")));
  // Not Huffman encoded short strings.
  EXPECT_CALL(*delegate(), OnInsertWithoutNameReference(Eq("foo"), Eq("foo")));
  // Not Huffman encoded long strings; length does not fit on prefix.
  // 'Z' would be Huffman encoded to 8 bits, so no Huffman encoding is used.
  EXPECT_CALL(*delegate(),
              OnInsertWithoutNameReference(Eq(std::string(31, 'Z')),
                                           Eq(std::string(127, 'Z'))));

  Decode(absl::HexStringToBytes(
      "4000"
      "4362617203626172"
      "6294e78294e7"
      "5f005a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a7f005a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"));
}

// Name Length value is too large for varint decoder to decode.
TEST_F(QpackEncoderStreamReceiverTest,
       InsertWithoutNameReferenceNameTooLongForVarintDecoder) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));

  Decode(absl::HexStringToBytes("5fffffffffffffffffffff"));
}

// Name Length value can be decoded by varint decoder but exceeds 1 MB limit.
TEST_F(QpackEncoderStreamReceiverTest,
       InsertWithoutNameReferenceNameExceedsLimit) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_STRING_LITERAL_TOO_LONG,
                              Eq("String literal too long.")));

  Decode(absl::HexStringToBytes("5fffff7f"));
}

// Value Length value is too large for varint decoder to decode.
TEST_F(QpackEncoderStreamReceiverTest,
       InsertWithoutNameReferenceValueTooLongForVarintDecoder) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));

  Decode(absl::HexStringToBytes("436261727fffffffffffffffffffff"));
}

// Value Length value can be decoded by varint decoder but exceeds 1 MB limit.
TEST_F(QpackEncoderStreamReceiverTest,
       InsertWithoutNameReferenceValueExceedsLimit) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_STRING_LITERAL_TOO_LONG,
                              Eq("String literal too long.")));

  Decode(absl::HexStringToBytes("436261727fffff7f"));
}

TEST_F(QpackEncoderStreamReceiverTest, Duplicate) {
  // Small index fits in prefix.
  EXPECT_CALL(*delegate(), OnDuplicate(17));
  // Large index requires two extension bytes.
  EXPECT_CALL(*delegate(), OnDuplicate(500));

  Decode(absl::HexStringToBytes("111fd503"));
}

TEST_F(QpackEncoderStreamReceiverTest, DuplicateIndexTooLarge) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));

  Decode(absl::HexStringToBytes("1fffffffffffffffffffff"));
}

TEST_F(QpackEncoderStreamReceiverTest, SetDynamicTableCapacity) {
  // Small capacity fits in prefix.
  EXPECT_CALL(*delegate(), OnSetDynamicTableCapacity(17));
  // Large capacity requires two extension bytes.
  EXPECT_CALL(*delegate(), OnSetDynamicTableCapacity(500));

  Decode(absl::HexStringToBytes("313fd503"));
}

TEST_F(QpackEncoderStreamReceiverTest, SetDynamicTableCapacityTooLarge) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_INTEGER_TOO_LARGE,
                              Eq("Encoded integer too large.")));

  Decode(absl::HexStringToBytes("3fffffffffffffffffffff"));
}

TEST_F(QpackEncoderStreamReceiverTest, InvalidHuffmanEncoding) {
  EXPECT_CALL(*delegate(),
              OnErrorDetected(QUIC_QPACK_ENCODER_STREAM_HUFFMAN_ENCODING_ERROR,
                              Eq("Error in Huffman-encoded string.")));

  Decode(absl::HexStringToBytes("c281ff"));
}

}  // namespace
}  // namespace test
}  // namespace quic
