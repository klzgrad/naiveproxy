// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder_stream_sender.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

using ::testing::Eq;
using ::testing::StrictMock;

namespace quic {
namespace test {
namespace {

class QpackEncoderStreamSenderTest : public QuicTest {
 protected:
  QpackEncoderStreamSenderTest() {
    stream_.set_qpack_stream_sender_delegate(&delegate_);
  }
  ~QpackEncoderStreamSenderTest() override = default;

  StrictMock<MockQpackStreamSenderDelegate> delegate_;
  QpackEncoderStreamSender stream_;
};

TEST_F(QpackEncoderStreamSenderTest, InsertWithNameReference) {
  // Static, index fits in prefix, empty value.
  std::string expected_encoded_data =
      quiche::QuicheTextUtils::HexDecode("c500");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithNameReference(true, 5, "");
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Static, index fits in prefix, Huffman encoded value.
  expected_encoded_data = quiche::QuicheTextUtils::HexDecode("c28294e7");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithNameReference(true, 2, "foo");
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Not static, index does not fit in prefix, not Huffman encoded value.
  expected_encoded_data = quiche::QuicheTextUtils::HexDecode("bf4a03626172");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithNameReference(false, 137, "bar");
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Value length does not fit in prefix.
  // 'Z' would be Huffman encoded to 8 bits, so no Huffman encoding is used.
  expected_encoded_data = quiche::QuicheTextUtils::HexDecode(
      "aa7f005a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithNameReference(false, 42, std::string(127, 'Z'));
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());
}

TEST_F(QpackEncoderStreamSenderTest, InsertWithoutNameReference) {
  // Empty name and value.
  std::string expected_encoded_data =
      quiche::QuicheTextUtils::HexDecode("4000");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithoutNameReference("", "");
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Huffman encoded short strings.
  expected_encoded_data = quiche::QuicheTextUtils::HexDecode("6294e78294e7");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithoutNameReference("foo", "foo");
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Not Huffman encoded short strings.
  expected_encoded_data =
      quiche::QuicheTextUtils::HexDecode("4362617203626172");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithoutNameReference("bar", "bar");
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Not Huffman encoded long strings; length does not fit on prefix.
  // 'Z' would be Huffman encoded to 8 bits, so no Huffman encoding is used.
  expected_encoded_data = quiche::QuicheTextUtils::HexDecode(
      "5f005a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a7f"
      "005a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"
      "5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendInsertWithoutNameReference(std::string(31, 'Z'),
                                         std::string(127, 'Z'));
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());
}

TEST_F(QpackEncoderStreamSenderTest, Duplicate) {
  // Small index fits in prefix.
  std::string expected_encoded_data = quiche::QuicheTextUtils::HexDecode("11");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendDuplicate(17);
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Large index requires two extension bytes.
  expected_encoded_data = quiche::QuicheTextUtils::HexDecode("1fd503");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendDuplicate(500);
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());
}

TEST_F(QpackEncoderStreamSenderTest, SetDynamicTableCapacity) {
  // Small capacity fits in prefix.
  std::string expected_encoded_data = quiche::QuicheTextUtils::HexDecode("31");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendSetDynamicTableCapacity(17);
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());

  // Large capacity requires two extension bytes.
  expected_encoded_data = quiche::QuicheTextUtils::HexDecode("3fd503");
  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  stream_.SendSetDynamicTableCapacity(500);
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());
}

// No writes should happen until Flush is called.
TEST_F(QpackEncoderStreamSenderTest, Coalesce) {
  // Insert entry with static name reference, empty value.
  stream_.SendInsertWithNameReference(true, 5, "");

  // Insert entry with static name reference, Huffman encoded value.
  stream_.SendInsertWithNameReference(true, 2, "foo");

  // Insert literal entry, Huffman encoded short strings.
  stream_.SendInsertWithoutNameReference("foo", "foo");

  // Duplicate entry.
  stream_.SendDuplicate(17);

  std::string expected_encoded_data = quiche::QuicheTextUtils::HexDecode(
      "c500"          // Insert entry with static name reference.
      "c28294e7"      // Insert entry with static name reference.
      "6294e78294e7"  // Insert literal entry.
      "11");          // Duplicate entry.

  EXPECT_CALL(delegate_, WriteStreamData(Eq(expected_encoded_data)));
  EXPECT_EQ(expected_encoded_data.size(), stream_.Flush());
}

// No writes should happen if QpackEncoderStreamSender::Flush() is called
// when the buffer is empty.
TEST_F(QpackEncoderStreamSenderTest, FlushEmpty) {
  EXPECT_EQ(0u, stream_.Flush());
}

}  // namespace
}  // namespace test
}  // namespace quic
