// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/qpack/qpack_instruction_decoder.h"

#include <algorithm>

#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "quic/core/qpack/qpack_instructions.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/platform/api/quic_test.h"
#include "quic/test_tools/qpack/qpack_test_utils.h"
#include "common/platform/api/quiche_text_utils.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::Expectation;
using ::testing::InvokeWithoutArgs;
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

  MOCK_METHOD(bool,
              OnInstructionDecoded,
              (const QpackInstruction*),
              (override));
  MOCK_METHOD(void,
              OnInstructionDecodingError,
              (QpackInstructionDecoder::ErrorCode error_code,
               absl::string_view error_message),
              (override));
};

class QpackInstructionDecoderTest : public QuicTestWithParam<FragmentMode> {
 protected:
  QpackInstructionDecoderTest()
      : decoder_(std::make_unique<QpackInstructionDecoder>(TestLanguage(),
                                                           &delegate_)),
        fragment_mode_(GetParam()) {}
  ~QpackInstructionDecoderTest() override = default;

  void SetUp() override {
    // Destroy QpackInstructionDecoder on error to test that it does not crash.
    // See https://crbug.com/1025209.
    ON_CALL(delegate_, OnInstructionDecodingError(_, _))
        .WillByDefault(InvokeWithoutArgs([this]() { decoder_.reset(); }));
  }

  // Decode one full instruction with fragment sizes dictated by
  // |fragment_mode_|.
  // Assumes that |data| is a single complete instruction, and accordingly
  // verifies that AtInstructionBoundary() returns true before and after the
  // instruction, and returns false while decoding is in progress.
  // Assumes that delegate methods destroy |decoder_| if they return false.
  void DecodeInstruction(absl::string_view data) {
    EXPECT_TRUE(decoder_->AtInstructionBoundary());

    FragmentSizeGenerator fragment_size_generator =
        FragmentModeToFragmentSizeGenerator(fragment_mode_);

    while (!data.empty()) {
      size_t fragment_size = std::min(fragment_size_generator(), data.size());
      bool success = decoder_->Decode(data.substr(0, fragment_size));
      if (!decoder_) {
        EXPECT_FALSE(success);
        return;
      }
      EXPECT_TRUE(success);
      data = data.substr(fragment_size);
      if (!data.empty()) {
        EXPECT_FALSE(decoder_->AtInstructionBoundary());
      }
    }

    EXPECT_TRUE(decoder_->AtInstructionBoundary());
  }

  StrictMock<MockDelegate> delegate_;
  std::unique_ptr<QpackInstructionDecoder> decoder_;

 private:
  const FragmentMode fragment_mode_;
};

INSTANTIATE_TEST_SUITE_P(All,
                         QpackInstructionDecoderTest,
                         Values(FragmentMode::kSingleChunk,
                                FragmentMode::kOctetByOctet));

TEST_P(QpackInstructionDecoderTest, SBitAndVarint2) {
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()));
  DecodeInstruction(absl::HexStringToBytes("7f01ff65"));

  EXPECT_TRUE(decoder_->s_bit());
  EXPECT_EQ(64u, decoder_->varint());
  EXPECT_EQ(356u, decoder_->varint2());

  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()));
  DecodeInstruction(absl::HexStringToBytes("05c8"));

  EXPECT_FALSE(decoder_->s_bit());
  EXPECT_EQ(5u, decoder_->varint());
  EXPECT_EQ(200u, decoder_->varint2());
}

TEST_P(QpackInstructionDecoderTest, NameAndValue) {
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction2()));
  DecodeInstruction(absl::HexStringToBytes("83666f6f03626172"));

  EXPECT_EQ("foo", decoder_->name());
  EXPECT_EQ("bar", decoder_->value());

  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction2()));
  DecodeInstruction(absl::HexStringToBytes("8000"));

  EXPECT_EQ("", decoder_->name());
  EXPECT_EQ("", decoder_->value());

  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction2()));
  DecodeInstruction(absl::HexStringToBytes("c294e7838c767f"));

  EXPECT_EQ("foo", decoder_->name());
  EXPECT_EQ("bar", decoder_->value());
}

TEST_P(QpackInstructionDecoderTest, InvalidHuffmanEncoding) {
  EXPECT_CALL(delegate_,
              OnInstructionDecodingError(
                  QpackInstructionDecoder::ErrorCode::HUFFMAN_ENCODING_ERROR,
                  Eq("Error in Huffman-encoded string.")));
  DecodeInstruction(absl::HexStringToBytes("c1ff"));
}

TEST_P(QpackInstructionDecoderTest, InvalidVarintEncoding) {
  EXPECT_CALL(delegate_,
              OnInstructionDecodingError(
                  QpackInstructionDecoder::ErrorCode::INTEGER_TOO_LARGE,
                  Eq("Encoded integer too large.")));
  DecodeInstruction(absl::HexStringToBytes("ffffffffffffffffffffff"));
}

TEST_P(QpackInstructionDecoderTest, StringLiteralTooLong) {
  EXPECT_CALL(delegate_,
              OnInstructionDecodingError(
                  QpackInstructionDecoder::ErrorCode::STRING_LITERAL_TOO_LONG,
                  Eq("String literal too long.")));
  DecodeInstruction(absl::HexStringToBytes("bfffff7f"));
}

TEST_P(QpackInstructionDecoderTest, DelegateSignalsError) {
  // First instruction is valid.
  Expectation first_call =
      EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()))
          .WillOnce(InvokeWithoutArgs([this]() -> bool {
            EXPECT_EQ(1u, decoder_->varint());
            return true;
          }));

  // Second instruction is invalid.  Decoding must halt.
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()))
      .After(first_call)
      .WillOnce(InvokeWithoutArgs([this]() -> bool {
        EXPECT_EQ(2u, decoder_->varint());
        return false;
      }));

  EXPECT_FALSE(
      decoder_->Decode(absl::HexStringToBytes("01000200030004000500")));
}

// QpackInstructionDecoder must not crash if it is destroyed from a
// Delegate::OnInstructionDecoded() call as long as it returns false.
TEST_P(QpackInstructionDecoderTest, DelegateSignalsErrorAndDestroysDecoder) {
  EXPECT_CALL(delegate_, OnInstructionDecoded(TestInstruction1()))
      .WillOnce(InvokeWithoutArgs([this]() -> bool {
        EXPECT_EQ(1u, decoder_->varint());
        decoder_.reset();
        return false;
      }));
  DecodeInstruction(absl::HexStringToBytes("0100"));
}

}  // namespace
}  // namespace test
}  // namespace quic
