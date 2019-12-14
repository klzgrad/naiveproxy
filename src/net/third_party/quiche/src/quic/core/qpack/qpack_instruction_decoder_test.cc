// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_decoder.h"

#include <algorithm>

#include "net/third_party/quiche/src/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::Expectation;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Values;

namespace quic {
namespace test {
namespace {

// This instruction has three fields: an S bit and two varints.
const QpackInstruction* TestInstruction1() {
  static const QpackInstruction* const instruction =
      new QpackInstruction{QpackInstructionOpcode{0x00, 0x80},
                           {{QpackInstructionFieldType::kSbit, 0x40},
                            {QpackInstructionFieldType::kVarint, 6},
                            {QpackInstructionFieldType::kVarint2, 8}}};
  return instruction;
}

// This instruction has two fields: a header name with a 6-bit prefix, and a
// header value with a 7-bit prefix, both preceded by a Huffman bit.
const QpackInstruction* TestInstruction2() {
  static const QpackInstruction* const instruction =
      new QpackInstruction{QpackInstructionOpcode{0x80, 0x80},
                           {{QpackInstructionFieldType::kName, 6},
                            {QpackInstructionFieldType::kValue, 7}}};
  return instruction;
}

const QpackLanguage* TestLanguage() {
  static const QpackLanguage* const language =
      new QpackLanguage{TestInstruction1(), TestInstruction2()};
  return language;
}

class MockDelegate : public QpackInstructionDecoder::Delegate {
 public:
  MockDelegate() {
    ON_CALL(*this, OnInstructionDecoded(_)).WillByDefault(Return(true));
  }

  MockDelegate(const MockDelegate&) = delete;
  MockDelegate& operator=(const MockDelegate&) = delete;
  ~MockDelegate() override = default;

  MOCK_METHOD1(OnInstructionDecoded, bool(const QpackInstruction* instruction));
  MOCK_METHOD1(OnError, void(QuicStringPiece error_message));
};

class QpackInstructionDecoderTest : public QuicTestWithParam<FragmentMode> {
 public:
  QpackInstructionDecoderTest()
      : decoder_(TestLanguage(), &delegate_), fragment_mode_(GetParam()) {}
  ~QpackInstructionDecoderTest() override = default;

 protected:
  // Decode one full instruction with fragment sizes dictated by
  // |fragment_mode_|.
  // Verifies that AtInstructionBoundary() returns true before and after the
  // instruction, and returns false while decoding is in progress.
  void DecodeInstruction(QuicStringPiece data) {
    EXPECT_TRUE(decoder_.AtInstructionBoundary());

    FragmentSizeGenerator fragment_size_generator =
        FragmentModeToFragmentSizeGenerator(fragment_mode_);

    while (!data.empty()) {
      size_t fragment_size = std::min(fragment_size_generator(), data.size());
      decoder_.Decode(data.substr(0, fragment_size));
      data = data.substr(fragment_size);
      if (!data.empty()) {
        EXPECT_FALSE(decoder_.AtInstructionBoundary());
      }
    }

    EXPECT_TRUE(decoder_.AtInstructionBoundary());
  }

  StrictMock<MockDelegate> delegate_;
  QpackInstructionDecoder decoder_;

 private:
  const FragmentMode fragment_mode_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         QpackInstructionDecoderTest,
                         Values(FragmentMode::kSingleChunk,
                                FragmentMode::kOctetByOctet));

TEST_P(QpackInstructionDecoderTest, SBitAndVarint2) {
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()));
  DecodeInstruction(QuicTextUtils::HexDecode("7f01ff65"));

  EXPECT_TRUE(decoder_.s_bit());
  EXPECT_EQ(64u, decoder_.varint());
  EXPECT_EQ(356u, decoder_.varint2());

  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()));
  DecodeInstruction(QuicTextUtils::HexDecode("05c8"));

  EXPECT_FALSE(decoder_.s_bit());
  EXPECT_EQ(5u, decoder_.varint());
  EXPECT_EQ(200u, decoder_.varint2());
}

TEST_P(QpackInstructionDecoderTest, NameAndValue) {
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction2()));
  DecodeInstruction(QuicTextUtils::HexDecode("83666f6f03626172"));

  EXPECT_EQ("foo", decoder_.name());
  EXPECT_EQ("bar", decoder_.value());

  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction2()));
  DecodeInstruction(QuicTextUtils::HexDecode("8000"));

  EXPECT_EQ("", decoder_.name());
  EXPECT_EQ("", decoder_.value());

  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction2()));
  DecodeInstruction(QuicTextUtils::HexDecode("c294e7838c767f"));

  EXPECT_EQ("foo", decoder_.name());
  EXPECT_EQ("bar", decoder_.value());
}

TEST_P(QpackInstructionDecoderTest, InvalidHuffmanEncoding) {
  EXPECT_CALL(delegate_, OnError(Eq("Error in Huffman-encoded string.")));
  decoder_.Decode(QuicTextUtils::HexDecode("c1ff"));
}

TEST_P(QpackInstructionDecoderTest, InvalidVarintEncoding) {
  EXPECT_CALL(delegate_, OnError(Eq("Encoded integer too large.")));
  decoder_.Decode(QuicTextUtils::HexDecode("ffffffffffffffffffffff"));
}

TEST_P(QpackInstructionDecoderTest, DelegateSignalsError) {
  // First instruction is valid.
  Expectation first_call =
      EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()))
          .WillOnce(Return(true));
  // Second instruction is invalid.  Decoding must halt.
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()))
      .After(first_call)
      .WillOnce(Return(false));
  decoder_.Decode(QuicTextUtils::HexDecode("01000200030004000500"));

  EXPECT_EQ(2u, decoder_.varint());
}

}  // namespace
}  // namespace test
}  // namespace quic
