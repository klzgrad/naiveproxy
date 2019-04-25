// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_instruction_encoder.h"

#include "base/logging.h"
#include "net/third_party/quic/core/qpack/qpack_test_utils.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::Values;

namespace quic {
namespace test {
namespace {

class QpackInstructionEncoderTest : public QuicTestWithParam<FragmentMode> {
 protected:
  QpackInstructionEncoderTest() : fragment_mode_(GetParam()) {}
  ~QpackInstructionEncoderTest() override = default;

  // Encode |instruction| with fragment sizes dictated by |fragment_mode_|.
  QuicString EncodeInstruction(const QpackInstruction* instruction) {
    EXPECT_FALSE(encoder_.HasNext());

    FragmentSizeGenerator fragment_size_generator =
        FragmentModeToFragmentSizeGenerator(fragment_mode_);
    QuicString output;
    encoder_.Encode(instruction);
    while (encoder_.HasNext()) {
      encoder_.Next(fragment_size_generator(), &output);
    }

    return output;
  }

  QpackInstructionEncoder encoder_;

 private:
  const FragmentMode fragment_mode_;
};

INSTANTIATE_TEST_SUITE_P(,
                         QpackInstructionEncoderTest,
                         Values(FragmentMode::kSingleChunk,
                                FragmentMode::kOctetByOctet));

TEST_P(QpackInstructionEncoderTest, Varint) {
  const QpackInstruction instruction{QpackInstructionOpcode{0x00, 0x80},
                                     {{QpackInstructionFieldType::kVarint, 7}}};

  encoder_.set_varint(5);
  EXPECT_EQ(QuicTextUtils::HexDecode("05"), EncodeInstruction(&instruction));

  encoder_.set_varint(127);
  EXPECT_EQ(QuicTextUtils::HexDecode("7f00"), EncodeInstruction(&instruction));
}

TEST_P(QpackInstructionEncoderTest, SBitAndTwoVarint2) {
  const QpackInstruction instruction{
      QpackInstructionOpcode{0x80, 0xc0},
      {{QpackInstructionFieldType::kSbit, 0x20},
       {QpackInstructionFieldType::kVarint, 5},
       {QpackInstructionFieldType::kVarint2, 8}}};

  encoder_.set_s_bit(true);
  encoder_.set_varint(5);
  encoder_.set_varint2(200);
  EXPECT_EQ(QuicTextUtils::HexDecode("a5c8"), EncodeInstruction(&instruction));

  encoder_.set_s_bit(false);
  encoder_.set_varint(31);
  encoder_.set_varint2(356);
  EXPECT_EQ(QuicTextUtils::HexDecode("9f00ff65"),
            EncodeInstruction(&instruction));
}

TEST_P(QpackInstructionEncoderTest, SBitAndVarintAndValue) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xc0, 0xc0},
                                     {{QpackInstructionFieldType::kSbit, 0x20},
                                      {QpackInstructionFieldType::kVarint, 5},
                                      {QpackInstructionFieldType::kValue, 7}}};

  encoder_.set_s_bit(true);
  encoder_.set_varint(100);
  encoder_.set_value("foo");
  EXPECT_EQ(QuicTextUtils::HexDecode("ff458294e7"),
            EncodeInstruction(&instruction));

  encoder_.set_s_bit(false);
  encoder_.set_varint(3);
  encoder_.set_value("bar");
  EXPECT_EQ(QuicTextUtils::HexDecode("c303626172"),
            EncodeInstruction(&instruction));
}

TEST_P(QpackInstructionEncoderTest, Name) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xe0, 0xe0},
                                     {{QpackInstructionFieldType::kName, 4}}};

  encoder_.set_name("");
  EXPECT_EQ(QuicTextUtils::HexDecode("e0"), EncodeInstruction(&instruction));

  encoder_.set_name("foo");
  EXPECT_EQ(QuicTextUtils::HexDecode("f294e7"),
            EncodeInstruction(&instruction));

  encoder_.set_name("bar");
  EXPECT_EQ(QuicTextUtils::HexDecode("e3626172"),
            EncodeInstruction(&instruction));
}

TEST_P(QpackInstructionEncoderTest, Value) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xf0, 0xf0},
                                     {{QpackInstructionFieldType::kValue, 3}}};

  encoder_.set_value("");
  EXPECT_EQ(QuicTextUtils::HexDecode("f0"), EncodeInstruction(&instruction));

  encoder_.set_value("foo");
  EXPECT_EQ(QuicTextUtils::HexDecode("fa94e7"),
            EncodeInstruction(&instruction));

  encoder_.set_value("bar");
  EXPECT_EQ(QuicTextUtils::HexDecode("f3626172"),
            EncodeInstruction(&instruction));
}

TEST_P(QpackInstructionEncoderTest, SBitAndNameAndValue) {
  const QpackInstruction instruction{QpackInstructionOpcode{0xf0, 0xf0},
                                     {{QpackInstructionFieldType::kSbit, 0x08},
                                      {QpackInstructionFieldType::kName, 2},
                                      {QpackInstructionFieldType::kValue, 7}}};

  encoder_.set_s_bit(false);
  encoder_.set_name("");
  encoder_.set_value("");
  EXPECT_EQ(QuicTextUtils::HexDecode("f000"), EncodeInstruction(&instruction));

  encoder_.set_s_bit(true);
  encoder_.set_name("foo");
  encoder_.set_value("bar");
  EXPECT_EQ(QuicTextUtils::HexDecode("fe94e703626172"),
            EncodeInstruction(&instruction));
}

}  // namespace
}  // namespace test
}  // namespace quic
