// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_decoder.h"

// Tests of HpackEntryDecoder.

#include <cstdint>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_collector.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"

using ::testing::AssertionResult;

namespace http2 {
namespace test {
namespace {

class HpackEntryDecoderTest : public RandomDecoderTest {
 protected:
  HpackEntryDecoderTest() : listener_(&collector_) {}

  DecodeStatus StartDecoding(DecodeBuffer* b) override {
    collector_.Clear();
    return decoder_.Start(b, &listener_);
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* b) override {
    return decoder_.Resume(b, &listener_);
  }

  AssertionResult DecodeAndValidateSeveralWays(DecodeBuffer* db,
                                               const Validator& validator) {
    // StartDecoding, above, requires the DecodeBuffer be non-empty so that it
    // can call Start with the prefix byte.
    bool return_non_zero_on_first = true;
    return RandomDecoderTest::DecodeAndValidateSeveralWays(
        db, return_non_zero_on_first, validator);
  }

  AssertionResult DecodeAndValidateSeveralWays(const HpackBlockBuilder& hbb,
                                               const Validator& validator) {
    DecodeBuffer db(hbb.buffer());
    return DecodeAndValidateSeveralWays(&db, validator);
  }

  HpackEntryDecoder decoder_;
  HpackEntryCollector collector_;
  HpackEntryDecoderVLoggingListener listener_;
};

TEST_F(HpackEntryDecoderTest, IndexedHeader_Literals) {
  {
    const char input[] = {'\x82'};  // == Index 2 ==
    DecodeBuffer b(input);
    auto do_check = [this]() {
      VERIFY_AND_RETURN_SUCCESS(collector_.ValidateIndexedHeader(2));
    };
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, ValidateDoneAndEmpty(do_check)));
    EXPECT_TRUE(do_check());
  }
  collector_.Clear();
  {
    const char input[] = {'\xfe'};  // == Index 126 ==
    DecodeBuffer b(input);
    auto do_check = [this]() {
      VERIFY_AND_RETURN_SUCCESS(collector_.ValidateIndexedHeader(126));
    };
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, ValidateDoneAndEmpty(do_check)));
    EXPECT_TRUE(do_check());
  }
  collector_.Clear();
  {
    const char input[] = {'\xff', '\x00'};  // == Index 127 ==
    DecodeBuffer b(input);
    auto do_check = [this]() {
      VERIFY_AND_RETURN_SUCCESS(collector_.ValidateIndexedHeader(127));
    };
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, ValidateDoneAndEmpty(do_check)));
    EXPECT_TRUE(do_check());
  }
}

TEST_F(HpackEntryDecoderTest, IndexedHeader_Various) {
  // Indices chosen to hit encoding and table boundaries.
  for (const uint32_t ndx : {1, 2, 61, 62, 63, 126, 127, 254, 255, 256}) {
    HpackBlockBuilder hbb;
    hbb.AppendIndexedHeader(ndx);

    auto do_check = [this, ndx]() {
      VERIFY_AND_RETURN_SUCCESS(collector_.ValidateIndexedHeader(ndx));
    };
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(hbb, ValidateDoneAndEmpty(do_check)));
    EXPECT_TRUE(do_check());
  }
}

TEST_F(HpackEntryDecoderTest, IndexedLiteralValue_Literal) {
  const char input[] =
      "\x7f"            // == Literal indexed, name index 0x40 ==
      "\x01"            // 2nd byte of name index (0x01 + 0x3f == 0x40)
      "\x0d"            // Value length (13)
      "custom-header";  // Value
  DecodeBuffer b(input, sizeof input - 1);
  auto do_check = [this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.ValidateLiteralValueHeader(
        HpackEntryType::kIndexedLiteralHeader, 0x40, false, "custom-header"));
  };
  EXPECT_TRUE(DecodeAndValidateSeveralWays(&b, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

TEST_F(HpackEntryDecoderTest, IndexedLiteralNameValue_Literal) {
  const char input[] =
      "\x40"            // == Literal indexed ==
      "\x0a"            // Name length (10)
      "custom-key"      // Name
      "\x0d"            // Value length (13)
      "custom-header";  // Value

  DecodeBuffer b(input, sizeof input - 1);
  auto do_check = [this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.ValidateLiteralNameValueHeader(
        HpackEntryType::kIndexedLiteralHeader, false, "custom-key", false,
        "custom-header"));
  };
  EXPECT_TRUE(DecodeAndValidateSeveralWays(&b, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

TEST_F(HpackEntryDecoderTest, DynamicTableSizeUpdate_Literal) {
  // Size update, length 31.
  const char input[] = "\x3f\x00";
  DecodeBuffer b(input, 2);
  auto do_check = [this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.ValidateDynamicTableSizeUpdate(31));
  };
  EXPECT_TRUE(DecodeAndValidateSeveralWays(&b, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

class HpackLiteralEntryDecoderTest
    : public HpackEntryDecoderTest,
      public ::testing::WithParamInterface<HpackEntryType> {
 protected:
  HpackLiteralEntryDecoderTest() : entry_type_(GetParam()) {}

  const HpackEntryType entry_type_;
};

INSTANTIATE_TEST_SUITE_P(
    AllLiteralTypes, HpackLiteralEntryDecoderTest,
    testing::Values(HpackEntryType::kIndexedLiteralHeader,
                    HpackEntryType::kUnindexedLiteralHeader,
                    HpackEntryType::kNeverIndexedLiteralHeader));

TEST_P(HpackLiteralEntryDecoderTest, RandNameIndexAndLiteralValue) {
  for (int n = 0; n < 10; n++) {
    const uint32_t ndx = 1 + Random().Rand8();
    const bool value_is_huffman_encoded = (n % 2) == 0;
    const std::string value = Random().RandString(Random().Rand8());
    HpackBlockBuilder hbb;
    hbb.AppendNameIndexAndLiteralValue(entry_type_, ndx,
                                       value_is_huffman_encoded, value);
    auto do_check = [this, ndx, value_is_huffman_encoded,
                     value]() -> AssertionResult {
      VERIFY_AND_RETURN_SUCCESS(collector_.ValidateLiteralValueHeader(
          entry_type_, ndx, value_is_huffman_encoded, value));
    };
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(hbb, ValidateDoneAndEmpty(do_check)));
    EXPECT_TRUE(do_check());
  }
}

TEST_P(HpackLiteralEntryDecoderTest, RandLiteralNameAndValue) {
  for (int n = 0; n < 10; n++) {
    const bool name_is_huffman_encoded = (n & 1) == 0;
    const int name_len = 1 + Random().Rand8();
    const std::string name = Random().RandString(name_len);
    const bool value_is_huffman_encoded = (n & 2) == 0;
    const int value_len = Random().Skewed(10);
    const std::string value = Random().RandString(value_len);
    HpackBlockBuilder hbb;
    hbb.AppendLiteralNameAndValue(entry_type_, name_is_huffman_encoded, name,
                                  value_is_huffman_encoded, value);
    auto do_check = [this, name_is_huffman_encoded, name,
                     value_is_huffman_encoded, value]() -> AssertionResult {
      VERIFY_AND_RETURN_SUCCESS(collector_.ValidateLiteralNameValueHeader(
          entry_type_, name_is_huffman_encoded, name, value_is_huffman_encoded,
          value));
    };
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(hbb, ValidateDoneAndEmpty(do_check)));
    EXPECT_TRUE(do_check());
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
