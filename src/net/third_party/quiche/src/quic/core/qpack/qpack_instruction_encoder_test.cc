// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_instruction_encoder.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

using ::testing::Values;

namespace quic {
namespace test {
namespace {

class QpackInstructionEncoderTest : public QuicTest {
 protected:
  QpackInstructionEncoderTest() : verified_position_(0) {}
  ~QpackInstructionEncoderTest() override = default;

  // Append encoded |instruction| to |output_|.
  void EncodeInstruction(const QpackInstruction* instruction,
                         const QpackInstructionEncoder::Values& values) {
    encoder_.Encode(instruction, values, &output_);
  }

  // Compare substring appended to |output_| since last EncodedSegmentMatches()
  // call against hex-encoded argument.
  bool EncodedSegmentMatches(QuicStringPiece hex_encoded_expected_substring) {
    auto recently_encoded = QuicStringPiece(output_).substr(verified_position_);
    auto expected = QuicTextUtils::HexDecode(hex_encoded_expected_substring);
    verified_position_ = output_.size();
    return recently_encoded == expected;
  }

 private:
  QpackInstructionEncoder encoder_;
  std::string output_;
  std::string::size_type verified_position_;
};

TEST_F(QpackInstructionEncoderTest, Varint) {
  const QpackInstruction instruction{QpackInstructionOpcode{0x00, 0x80},
                                     {{QpackInstructionFieldType::kVarint, 7}}};

  QpackInstructionEncoder::Values values;
  values.varint = 5;
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("05"));

  values.varint = 127;
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("7f00"));
}

TEST_F(QpackInstructionEncoderTest, SBitAndTwoVarint2) {
  const QpackInstruction instruction{
      QpackInstructionOpcode{0x80, 0xc0},
      {{QpackInstructionFieldType::kSbit, 0x20},
       {QpackInstructionFieldType::kVarint, 5},
       {QpackInstructionFieldType::kVarint2, 8}}};

  QpackInstructionEncoder::Values values;
  values.s_bit = true;
  values.varint = 5;
  values.varint2 = 200;
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("a5c8"));

  values.s_bit = false;
  values.varint = 31;
  values.varint2 = 356;
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("9f00ff65"));
}

TEST_F(QpackInstructionEncoderTest, SBitAndVarintAndValue) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xc0, 0xc0},
                                     {{QpackInstructionFieldType::kSbit, 0x20},
                                      {QpackInstructionFieldType::kVarint, 5},
                                      {QpackInstructionFieldType::kValue, 7}}};

  QpackInstructionEncoder::Values values;
  values.s_bit = true;
  values.varint = 100;
  values.value = "foo";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("ff458294e7"));

  values.s_bit = false;
  values.varint = 3;
  values.value = "bar";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("c303626172"));
}

TEST_F(QpackInstructionEncoderTest, Name) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xe0, 0xe0},
                                     {{QpackInstructionFieldType::kName, 4}}};

  QpackInstructionEncoder::Values values;
  values.name = "";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("e0"));

  values.name = "foo";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("f294e7"));

  values.name = "bar";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("e3626172"));
}

TEST_F(QpackInstructionEncoderTest, Value) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xf0, 0xf0},
                                     {{QpackInstructionFieldType::kValue, 3}}};

  QpackInstructionEncoder::Values values;
  values.value = "";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("f0"));

  values.value = "foo";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("fa94e7"));

  values.value = "bar";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("f3626172"));
}

TEST_F(QpackInstructionEncoderTest, SBitAndNameAndValue) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xf0, 0xf0},
                                     {{QpackInstructionFieldType::kSbit, 0x08},
                                      {QpackInstructionFieldType::kName, 2},
                                      {QpackInstructionFieldType::kValue, 7}}};

  QpackInstructionEncoder::Values values;
  values.s_bit = false;
  values.name = "";
  values.value = "";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("f000"));

  values.s_bit = true;
  values.name = "foo";
  values.value = "bar";
  EncodeInstruction(&instruction, values);
  EXPECT_TRUE(EncodedSegmentMatches("fe94e703626172"));
}

}  // namespace
}  // namespace test
}  // namespace quic
