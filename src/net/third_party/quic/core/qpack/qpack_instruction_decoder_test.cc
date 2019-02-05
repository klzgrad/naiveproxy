// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_instruction_decoder.h"

#include "base/logging.h"
#include "net/third_party/quic/core/qpack/qpack_constants.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::Expectation;
using ::testing::Return;
using ::testing::StrictMock;

namespace quic {
namespace test {
namespace {

// This instruction has two fields: a static bit and a 6-bit prefix encoded
// index.
const QpackInstruction* TestInstruction1() {
  static const QpackInstruction* const instruction = []() {
    auto* instruction =
        new QpackInstruction{QpackInstructionOpcode{0x00, 0x80},
                             {{QpackInstructionFieldType::kStaticBit, 0x40},
                              {QpackInstructionFieldType::kVarint, 6}}};
    return instruction;
  }();
  return instruction;
}

// This instruction has two fields: a header name with a 6-bit prefix, and a
// header value with a 7-bit prefix, both preceded by a Huffman bit.
const QpackInstruction* TestInstruction2() {
  static const QpackInstruction* const instruction = []() {
    auto* instruction =
        new QpackInstruction{QpackInstructionOpcode{0x80, 0x80},
                             {{QpackInstructionFieldType::kName, 6},
                              {QpackInstructionFieldType::kValue, 7}}};
    return instruction;
  }();
  return instruction;
}

const QpackLanguage* TestLanguage() {
  static const QpackLanguage* const language = []() {
    auto* language = new QpackLanguage{TestInstruction1(), TestInstruction2()};
    return language;
  }();
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

class QpackInstructionDecoderTest : public QuicTest {
 public:
  QpackInstructionDecoderTest() : decoder_(TestLanguage(), &delegate_) {}

 protected:
  // Decode one full instruction byte by byte.  Verifies that
  // AtInstructionBoundary() returns true before and after the instruction, and
  // returns false in while decoding is in progress.
  void DecodeInstruction(QuicStringPiece data) {
    for (QuicStringPiece::size_type i = 0; i < data.size(); ++i) {
      if (i == 0) {
        EXPECT_TRUE(decoder_.AtInstructionBoundary());
      } else {
        EXPECT_FALSE(decoder_.AtInstructionBoundary());
      }
      decoder_.Decode(data.substr(i, 1));
    }
    EXPECT_TRUE(decoder_.AtInstructionBoundary());
  }

  StrictMock<MockDelegate> delegate_;
  QpackInstructionDecoder decoder_;
};

TEST_F(QpackInstructionDecoderTest, StaticBitAndIndex) {
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()));
  DecodeInstruction(QuicTextUtils::HexDecode("7f01"));

  EXPECT_TRUE(decoder_.is_static());
  EXPECT_EQ(64u, decoder_.varint());

  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()));
  DecodeInstruction(QuicTextUtils::HexDecode("05"));

  EXPECT_FALSE(decoder_.is_static());
  EXPECT_EQ(5u, decoder_.varint());
}

TEST_F(QpackInstructionDecoderTest, NameAndValue) {
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

TEST_F(QpackInstructionDecoderTest, InvalidHuffmanEncoding) {
  EXPECT_CALL(delegate_,
              OnError(QuicStringPiece("Error in Huffman-encoded string.")));
  decoder_.Decode(QuicTextUtils::HexDecode("c1ff"));
}

TEST_F(QpackInstructionDecoderTest, InvalidVarintEncoding) {
  EXPECT_CALL(delegate_,
              OnError(QuicStringPiece("Encoded integer too large.")));
  decoder_.Decode(QuicTextUtils::HexDecode("ffffffffffffffffffffff"));
}

TEST_F(QpackInstructionDecoderTest, DelegateSignalsError) {
  // First instruction is valid.
  Expectation first_call =
      EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()))
          .WillOnce(Return(true));
  // Second instruction is invalid.  Decoding must halt.
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()))
      .After(first_call)
      .WillOnce(Return(false));
  decoder_.Decode(QuicTextUtils::HexDecode("0102030405"));

  EXPECT_EQ(2u, decoder_.varint());
}

}  // namespace
}  // namespace test
}  // namespace quic
