// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_block_decoder.h"

// Tests of HpackBlockDecoder.

#include <cstdint>
#include <string>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_block_collector.h"
#include "net/third_party/quiche/src/http2/hpack/http2_hpack_constants.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_example.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using ::testing::AssertionSuccess;

namespace http2 {
namespace test {
namespace {

class HpackBlockDecoderTest : public RandomDecoderTest {
 protected:
  HpackBlockDecoderTest() : listener_(&collector_), decoder_(&listener_) {
    stop_decode_on_done_ = false;
    decoder_.Reset();
    // Make sure logging doesn't crash. Not examining the result.
    std::ostringstream strm;
    strm << decoder_;
  }

  DecodeStatus StartDecoding(DecodeBuffer* db) override {
    collector_.Clear();
    decoder_.Reset();
    return ResumeDecoding(db);
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* db) override {
    DecodeStatus status = decoder_.Decode(db);

    // Make sure logging doesn't crash. Not examining the result.
    std::ostringstream strm;
    strm << decoder_;

    return status;
  }

  AssertionResult DecodeAndValidateSeveralWays(DecodeBuffer* db,
                                               const Validator& validator) {
    bool return_non_zero_on_first = false;
    return RandomDecoderTest::DecodeAndValidateSeveralWays(
        db, return_non_zero_on_first, validator);
  }

  AssertionResult DecodeAndValidateSeveralWays(const HpackBlockBuilder& hbb,
                                               const Validator& validator) {
    DecodeBuffer db(hbb.buffer());
    return DecodeAndValidateSeveralWays(&db, validator);
  }

  AssertionResult DecodeHpackExampleAndValidateSeveralWays(
      quiche::QuicheStringPiece hpack_example,
      Validator validator) {
    std::string input = HpackExampleToStringOrDie(hpack_example);
    DecodeBuffer db(input);
    return DecodeAndValidateSeveralWays(&db, validator);
  }

  uint8_t Rand8() { return Random().Rand8(); }

  std::string Rand8String() { return Random().RandString(Rand8()); }

  HpackBlockCollector collector_;
  HpackEntryDecoderVLoggingListener listener_;
  HpackBlockDecoder decoder_;
};

// http://httpwg.org/specs/rfc7541.html#rfc.section.C.2.1
TEST_F(HpackBlockDecoderTest, SpecExample_C_2_1) {
  auto do_check = [this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.ValidateSoleLiteralNameValueHeader(
        HpackEntryType::kIndexedLiteralHeader, false, "custom-key", false,
        "custom-header"));
  };
  const char hpack_example[] = R"(
      40                                      | == Literal indexed ==
      0a                                      |   Literal name (len = 10)
      6375 7374 6f6d 2d6b 6579                | custom-key
      0d                                      |   Literal value (len = 13)
      6375 7374 6f6d 2d68 6561 6465 72        | custom-header
                                              | -> custom-key:
                                              |   custom-header
      )";
  EXPECT_TRUE(DecodeHpackExampleAndValidateSeveralWays(
      hpack_example, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

// http://httpwg.org/specs/rfc7541.html#rfc.section.C.2.2
TEST_F(HpackBlockDecoderTest, SpecExample_C_2_2) {
  auto do_check = [this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.ValidateSoleLiteralValueHeader(
        HpackEntryType::kUnindexedLiteralHeader, 4, false, "/sample/path"));
  };
  const char hpack_example[] = R"(
      04                                      | == Literal not indexed ==
                                              |   Indexed name (idx = 4)
                                              |     :path
      0c                                      |   Literal value (len = 12)
      2f73 616d 706c 652f 7061 7468           | /sample/path
                                              | -> :path: /sample/path
      )";
  EXPECT_TRUE(DecodeHpackExampleAndValidateSeveralWays(
      hpack_example, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

// http://httpwg.org/specs/rfc7541.html#rfc.section.C.2.3
TEST_F(HpackBlockDecoderTest, SpecExample_C_2_3) {
  auto do_check = [this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.ValidateSoleLiteralNameValueHeader(
        HpackEntryType::kNeverIndexedLiteralHeader, false, "password", false,
        "secret"));
  };
  const char hpack_example[] = R"(
      10                                      | == Literal never indexed ==
      08                                      |   Literal name (len = 8)
      7061 7373 776f 7264                     | password
      06                                      |   Literal value (len = 6)
      7365 6372 6574                          | secret
                                              | -> password: secret
      )";
  EXPECT_TRUE(DecodeHpackExampleAndValidateSeveralWays(
      hpack_example, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

// http://httpwg.org/specs/rfc7541.html#rfc.section.C.2.4
TEST_F(HpackBlockDecoderTest, SpecExample_C_2_4) {
  auto do_check = [this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.ValidateSoleIndexedHeader(2));
  };
  const char hpack_example[] = R"(
      82                                      | == Indexed - Add ==
                                              |   idx = 2
                                              | -> :method: GET
      )";
  EXPECT_TRUE(DecodeHpackExampleAndValidateSeveralWays(
      hpack_example, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}
// http://httpwg.org/specs/rfc7541.html#rfc.section.C.3.1
TEST_F(HpackBlockDecoderTest, SpecExample_C_3_1) {
  std::string example = R"(
      82                                      | == Indexed - Add ==
                                              |   idx = 2
                                              | -> :method: GET
      86                                      | == Indexed - Add ==
                                              |   idx = 6
                                              | -> :scheme: http
      84                                      | == Indexed - Add ==
                                              |   idx = 4
                                              | -> :path: /
      41                                      | == Literal indexed ==
                                              |   Indexed name (idx = 1)
                                              |     :authority
      0f                                      |   Literal value (len = 15)
      7777 772e 6578 616d 706c 652e 636f 6d   | www.example.com
                                              | -> :authority:
                                              |   www.example.com
      )";
  HpackBlockCollector expected;
  expected.ExpectIndexedHeader(2);
  expected.ExpectIndexedHeader(6);
  expected.ExpectIndexedHeader(4);
  expected.ExpectNameIndexAndLiteralValue(HpackEntryType::kIndexedLiteralHeader,
                                          1, false, "www.example.com");
  NoArgValidator do_check = [expected, this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.VerifyEq(expected));
  };
  EXPECT_TRUE(DecodeHpackExampleAndValidateSeveralWays(
      example, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

// http://httpwg.org/specs/rfc7541.html#rfc.section.C.5.1
TEST_F(HpackBlockDecoderTest, SpecExample_C_5_1) {
  std::string example = R"(
      48                                      | == Literal indexed ==
                                              |   Indexed name (idx = 8)
                                              |     :status
      03                                      |   Literal value (len = 3)
      3330 32                                 | 302
                                              | -> :status: 302
      58                                      | == Literal indexed ==
                                              |   Indexed name (idx = 24)
                                              |     cache-control
      07                                      |   Literal value (len = 7)
      7072 6976 6174 65                       | private
                                              | -> cache-control: private
      61                                      | == Literal indexed ==
                                              |   Indexed name (idx = 33)
                                              |     date
      1d                                      |   Literal value (len = 29)
      4d6f 6e2c 2032 3120 4f63 7420 3230 3133 | Mon, 21 Oct 2013
      2032 303a 3133 3a32 3120 474d 54        |  20:13:21 GMT
                                              | -> date: Mon, 21 Oct 2013
                                              |   20:13:21 GMT
      6e                                      | == Literal indexed ==
                                              |   Indexed name (idx = 46)
                                              |     location
      17                                      |   Literal value (len = 23)
      6874 7470 733a 2f2f 7777 772e 6578 616d | https://www.exam
      706c 652e 636f 6d                       | ple.com
                                              | -> location:
                                              |   https://www.example.com
      )";
  HpackBlockCollector expected;
  expected.ExpectNameIndexAndLiteralValue(HpackEntryType::kIndexedLiteralHeader,
                                          8, false, "302");
  expected.ExpectNameIndexAndLiteralValue(HpackEntryType::kIndexedLiteralHeader,
                                          24, false, "private");
  expected.ExpectNameIndexAndLiteralValue(HpackEntryType::kIndexedLiteralHeader,
                                          33, false,
                                          "Mon, 21 Oct 2013 20:13:21 GMT");
  expected.ExpectNameIndexAndLiteralValue(HpackEntryType::kIndexedLiteralHeader,
                                          46, false, "https://www.example.com");
  NoArgValidator do_check = [expected, this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.VerifyEq(expected));
  };
  EXPECT_TRUE(DecodeHpackExampleAndValidateSeveralWays(
      example, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

// Generate a bunch of HPACK block entries to expect, use those expectations
// to generate an HPACK block, then decode it and confirm it matches those
// expectations. Some of these are invalid (such as Indexed, with index=0),
// but well-formed, and the decoder doesn't check for validity, just
// well-formedness. That includes the validity of the strings not being checked,
// such as lower-case ascii for the names, and valid Huffman encodings.
TEST_F(HpackBlockDecoderTest, Computed) {
  HpackBlockCollector expected;
  expected.ExpectIndexedHeader(0);
  expected.ExpectIndexedHeader(1);
  expected.ExpectIndexedHeader(126);
  expected.ExpectIndexedHeader(127);
  expected.ExpectIndexedHeader(128);
  expected.ExpectDynamicTableSizeUpdate(0);
  expected.ExpectDynamicTableSizeUpdate(1);
  expected.ExpectDynamicTableSizeUpdate(14);
  expected.ExpectDynamicTableSizeUpdate(15);
  expected.ExpectDynamicTableSizeUpdate(30);
  expected.ExpectDynamicTableSizeUpdate(31);
  expected.ExpectDynamicTableSizeUpdate(4095);
  expected.ExpectDynamicTableSizeUpdate(4096);
  expected.ExpectDynamicTableSizeUpdate(8192);
  for (auto type : {HpackEntryType::kIndexedLiteralHeader,
                    HpackEntryType::kUnindexedLiteralHeader,
                    HpackEntryType::kNeverIndexedLiteralHeader}) {
    for (bool value_huffman : {false, true}) {
      // An entry with an index for the name. Ensure the name index
      // is not zero by adding one to the Rand8() result.
      expected.ExpectNameIndexAndLiteralValue(type, Rand8() + 1, value_huffman,
                                              Rand8String());
      // And two entries with literal names, one plain, one huffman encoded.
      expected.ExpectLiteralNameAndValue(type, false, Rand8String(),
                                         value_huffman, Rand8String());
      expected.ExpectLiteralNameAndValue(type, true, Rand8String(),
                                         value_huffman, Rand8String());
    }
  }
  // Shuffle the entries and serialize them to produce an HPACK block.
  expected.ShuffleEntries(RandomPtr());
  HpackBlockBuilder hbb;
  expected.AppendToHpackBlockBuilder(&hbb);

  NoArgValidator do_check = [expected, this]() {
    VERIFY_AND_RETURN_SUCCESS(collector_.VerifyEq(expected));
  };
  EXPECT_TRUE(
      DecodeAndValidateSeveralWays(hbb, ValidateDoneAndEmpty(do_check)));
  EXPECT_TRUE(do_check());
}

}  // namespace
}  // namespace test
}  // namespace http2
