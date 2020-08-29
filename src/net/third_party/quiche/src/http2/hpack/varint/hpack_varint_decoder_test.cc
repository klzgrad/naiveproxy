// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_decoder.h"

// Test HpackVarintDecoder against hardcoded data.

#include <stddef.h>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using ::testing::AssertionFailure;
using ::testing::AssertionSuccess;

namespace http2 {
namespace test {
namespace {

class HpackVarintDecoderTest : public RandomDecoderTest,
                               public ::testing::WithParamInterface<
                                   ::testing::tuple<uint8_t, const char*>> {
 protected:
  HpackVarintDecoderTest()
      : high_bits_(::testing::get<0>(GetParam())),
        suffix_(Http2HexDecode(::testing::get<1>(GetParam()))),
        prefix_length_(0) {}

  void DecodeExpectSuccess(quiche::QuicheStringPiece data,
                           uint32_t prefix_length,
                           uint64_t expected_value) {
    Validator validator = [expected_value, this](
                              const DecodeBuffer& db,
                              DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(expected_value, decoder_.value())
          << "Value doesn't match expected: " << decoder_.value()
          << " != " << expected_value;
      return AssertionSuccess();
    };

    // First validate that decoding is done and that we've advanced the cursor
    // the expected amount.
    validator = ValidateDoneAndOffset(/* offset = */ data.size(), validator);

    EXPECT_TRUE(Decode(data, prefix_length, validator));

    EXPECT_EQ(expected_value, decoder_.value());
  }

  void DecodeExpectError(quiche::QuicheStringPiece data,
                         uint32_t prefix_length) {
    Validator validator = [](const DecodeBuffer& db,
                             DecodeStatus status) -> AssertionResult {
      VERIFY_EQ(DecodeStatus::kDecodeError, status);
      return AssertionSuccess();
    };

    EXPECT_TRUE(Decode(data, prefix_length, validator));
  }

 private:
  AssertionResult Decode(quiche::QuicheStringPiece data,
                         uint32_t prefix_length,
                         const Validator validator) {
    prefix_length_ = prefix_length;

    // Copy |data| so that it can be modified.
    std::string data_copy(data);

    // Bits of the first byte not part of the prefix should be ignored.
    uint8_t high_bits_mask = 0b11111111 << prefix_length_;
    data_copy[0] |= (high_bits_mask & high_bits_);

    // Extra bytes appended to the input should be ignored.
    data_copy.append(suffix_);

    DecodeBuffer b(data_copy);

    // StartDecoding, above, requires the DecodeBuffer be non-empty so that it
    // can call Start with the prefix byte.
    bool return_non_zero_on_first = true;

    return DecodeAndValidateSeveralWays(&b, return_non_zero_on_first,
                                        validator);
  }

  DecodeStatus StartDecoding(DecodeBuffer* b) override {
    CHECK_LT(0u, b->Remaining());
    uint8_t prefix = b->DecodeUInt8();
    return decoder_.Start(prefix, prefix_length_, b);
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* b) override {
    return decoder_.Resume(b);
  }

  // Bits of the first byte not part of the prefix.
  const uint8_t high_bits_;
  // Extra bytes appended to the input.
  const std::string suffix_;

  HpackVarintDecoder decoder_;
  uint8_t prefix_length_;
};

INSTANTIATE_TEST_SUITE_P(
    HpackVarintDecoderTest,
    HpackVarintDecoderTest,
    ::testing::Combine(
        // Bits of the first byte not part of the prefix should be ignored.
        ::testing::Values(0b00000000, 0b11111111, 0b10101010),
        // Extra bytes appended to the input should be ignored.
        ::testing::Values("", "00", "666f6f")));

struct {
  const char* data;
  uint32_t prefix_length;
  uint64_t expected_value;
} kSuccessTestData[] = {
    // Zero value with different prefix lengths.
    {"00", 3, 0},
    {"00", 4, 0},
    {"00", 5, 0},
    {"00", 6, 0},
    {"00", 7, 0},
    {"00", 8, 0},
    // Small values that fit in the prefix.
    {"06", 3, 6},
    {"0d", 4, 13},
    {"10", 5, 16},
    {"29", 6, 41},
    {"56", 7, 86},
    {"bf", 8, 191},
    // Values of 2^n-1, which have an all-zero extension byte.
    {"0700", 3, 7},
    {"0f00", 4, 15},
    {"1f00", 5, 31},
    {"3f00", 6, 63},
    {"7f00", 7, 127},
    {"ff00", 8, 255},
    // Values of 2^n-1, plus one extra byte of padding.
    {"078000", 3, 7},
    {"0f8000", 4, 15},
    {"1f8000", 5, 31},
    {"3f8000", 6, 63},
    {"7f8000", 7, 127},
    {"ff8000", 8, 255},
    // Values requiring one extension byte.
    {"0760", 3, 103},
    {"0f2a", 4, 57},
    {"1f7f", 5, 158},
    {"3f02", 6, 65},
    {"7f49", 7, 200},
    {"ff6f", 8, 366},
    // Values requiring one extension byte, plus one byte of padding.
    {"07e000", 3, 103},
    {"0faa00", 4, 57},
    {"1fff00", 5, 158},
    {"3f8200", 6, 65},
    {"7fc900", 7, 200},
    {"ffef00", 8, 366},
    // Values requiring one extension byte, plus two bytes of padding.
    {"07e08000", 3, 103},
    {"0faa8000", 4, 57},
    {"1fff8000", 5, 158},
    {"3f828000", 6, 65},
    {"7fc98000", 7, 200},
    {"ffef8000", 8, 366},
    // Values requiring one extension byte, plus the maximum amount of padding.
    {"07e0808080808080808000", 3, 103},
    {"0faa808080808080808000", 4, 57},
    {"1fff808080808080808000", 5, 158},
    {"3f82808080808080808000", 6, 65},
    {"7fc9808080808080808000", 7, 200},
    {"ffef808080808080808000", 8, 366},
    // Values requiring two extension bytes.
    {"07b260", 3, 12345},
    {"0f8a2a", 4, 5401},
    {"1fa87f", 5, 16327},
    {"3fd002", 6, 399},
    {"7fff49", 7, 9598},
    {"ffe32f", 8, 6370},
    // Values requiring two extension bytes, plus one byte of padding.
    {"07b2e000", 3, 12345},
    {"0f8aaa00", 4, 5401},
    {"1fa8ff00", 5, 16327},
    {"3fd08200", 6, 399},
    {"7fffc900", 7, 9598},
    {"ffe3af00", 8, 6370},
    // Values requiring two extension bytes, plus the maximum amount of padding.
    {"07b2e080808080808000", 3, 12345},
    {"0f8aaa80808080808000", 4, 5401},
    {"1fa8ff80808080808000", 5, 16327},
    {"3fd08280808080808000", 6, 399},
    {"7fffc980808080808000", 7, 9598},
    {"ffe3af80808080808000", 8, 6370},
    // Values requiring three extension bytes.
    {"078ab260", 3, 1579281},
    {"0fc18a2a", 4, 689488},
    {"1fada87f", 5, 2085964},
    {"3fa0d002", 6, 43103},
    {"7ffeff49", 7, 1212541},
    {"ff93de23", 8, 585746},
    // Values requiring three extension bytes, plus one byte of padding.
    {"078ab2e000", 3, 1579281},
    {"0fc18aaa00", 4, 689488},
    {"1fada8ff00", 5, 2085964},
    {"3fa0d08200", 6, 43103},
    {"7ffeffc900", 7, 1212541},
    {"ff93dea300", 8, 585746},
    // Values requiring four extension bytes.
    {"079f8ab260", 3, 202147110},
    {"0fa2c18a2a", 4, 88252593},
    {"1fd0ada87f", 5, 266999535},
    {"3ff9a0d002", 6, 5509304},
    {"7f9efeff49", 7, 155189149},
    {"ffaa82f404", 8, 10289705},
    // Values requiring four extension bytes, plus one byte of padding.
    {"079f8ab2e000", 3, 202147110},
    {"0fa2c18aaa00", 4, 88252593},
    {"1fd0ada8ff00", 5, 266999535},
    {"3ff9a0d08200", 6, 5509304},
    {"7f9efeffc900", 7, 155189149},
    {"ffaa82f48400", 8, 10289705},
    // Values requiring six extension bytes.
    {"0783aa9f8ab260", 3, 3311978140938},
    {"0ff0b0a2c18a2a", 4, 1445930244223},
    {"1fda84d0ada87f", 5, 4374519874169},
    {"3fb5fbf9a0d002", 6, 90263420404},
    {"7fcff19efeff49", 7, 2542616951118},
    {"ff9fa486bbc327", 8, 1358138807070},
    // Values requiring eight extension bytes.
    {"07f19883aa9f8ab260", 3, 54263449861016696},
    {"0f84fdf0b0a2c18a2a", 4, 23690121121119891},
    {"1fa0dfda84d0ada87f", 5, 71672133617889215},
    {"3f9ff0b5fbf9a0d002", 6, 1478875878881374},
    {"7ffbc1cff19efeff49", 7, 41658236125045114},
    {"ff91b6fb85af99c342", 8, 37450237664484368},
    // Values requiring ten extension bytes.
    {"0794f1f19883aa9f8ab201", 3, 12832019021693745307u},
    {"0fa08f84fdf0b0a2c18a01", 4, 9980690937382242223u},
    {"1fbfdda0dfda84d0ada801", 5, 12131360551794650846u},
    {"3f9dc79ff0b5fbf9a0d001", 6, 15006530362736632796u},
    {"7f8790fbc1cff19efeff01", 7, 18445754019193211014u},
    {"fffba8c5b8d3fe9f8c8401", 8, 9518498503615141242u},
    // Maximum value: 2^64-1.
    {"07f8ffffffffffffffff01", 3, 18446744073709551615u},
    {"0ff0ffffffffffffffff01", 4, 18446744073709551615u},
    {"1fe0ffffffffffffffff01", 5, 18446744073709551615u},
    {"3fc0ffffffffffffffff01", 6, 18446744073709551615u},
    {"7f80ffffffffffffffff01", 7, 18446744073709551615u},
    {"ff80feffffffffffffff01", 8, 18446744073709551615u},
    // Examples from RFC7541 C.1.
    {"0a", 5, 10},
    {"1f9a0a", 5, 1337},
};

TEST_P(HpackVarintDecoderTest, Success) {
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kSuccessTestData); ++i) {
    DecodeExpectSuccess(Http2HexDecode(kSuccessTestData[i].data),
                        kSuccessTestData[i].prefix_length,
                        kSuccessTestData[i].expected_value);
  }
}

struct {
  const char* data;
  uint32_t prefix_length;
} kErrorTestData[] = {
    // Too many extension bytes, all 0s (except for extension bit in each byte).
    {"0780808080808080808080", 3},
    {"0f80808080808080808080", 4},
    {"1f80808080808080808080", 5},
    {"3f80808080808080808080", 6},
    {"7f80808080808080808080", 7},
    {"ff80808080808080808080", 8},
    // Too many extension bytes, all 1s.
    {"07ffffffffffffffffffff", 3},
    {"0fffffffffffffffffffff", 4},
    {"1fffffffffffffffffffff", 5},
    {"3fffffffffffffffffffff", 6},
    {"7fffffffffffffffffffff", 7},
    {"ffffffffffffffffffffff", 8},
    // Value of 2^64, one higher than maximum of 2^64-1.
    {"07f9ffffffffffffffff01", 3},
    {"0ff1ffffffffffffffff01", 4},
    {"1fe1ffffffffffffffff01", 5},
    {"3fc1ffffffffffffffff01", 6},
    {"7f81ffffffffffffffff01", 7},
    {"ff81feffffffffffffff01", 8},
    // Maximum value: 2^64-1, with one byte of padding.
    {"07f8ffffffffffffffff8100", 3},
    {"0ff0ffffffffffffffff8100", 4},
    {"1fe0ffffffffffffffff8100", 5},
    {"3fc0ffffffffffffffff8100", 6},
    {"7f80ffffffffffffffff8100", 7},
    {"ff80feffffffffffffff8100", 8}};

TEST_P(HpackVarintDecoderTest, Error) {
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kErrorTestData); ++i) {
    DecodeExpectError(Http2HexDecode(kErrorTestData[i].data),
                      kErrorTestData[i].prefix_length);
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
