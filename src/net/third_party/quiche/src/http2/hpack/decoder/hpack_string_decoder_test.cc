// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_string_decoder.h"

// Tests of HpackStringDecoder.

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_string_collector.h"
#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_string_decoder_listener.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/test_tools/http2_random.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

using ::testing::AssertionResult;

namespace http2 {
namespace test {
namespace {

const bool kMayReturnZeroOnFirst = false;
const bool kCompressed = true;
const bool kUncompressed = false;

class HpackStringDecoderTest : public RandomDecoderTest {
 protected:
  HpackStringDecoderTest() : listener_(&collector_) {}

  DecodeStatus StartDecoding(DecodeBuffer* b) override {
    ++start_decoding_calls_;
    collector_.Clear();
    return decoder_.Start(b, &listener_);
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* b) override {
    // Provides coverage of DebugString and StateToString.
    // Not validating output.
    HTTP2_VLOG(1) << decoder_.DebugString();
    HTTP2_VLOG(2) << collector_;
    return decoder_.Resume(b, &listener_);
  }

  AssertionResult Collected(quiche::QuicheStringPiece s, bool huffman_encoded) {
    HTTP2_VLOG(1) << collector_;
    return collector_.Collected(s, huffman_encoded);
  }

  // expected_str is a std::string rather than a const std::string& or
  // QuicheStringPiece so that the lambda makes a copy of the string, and thus
  // the string to be passed to Collected outlives the call to MakeValidator.
  Validator MakeValidator(const std::string& expected_str,
                          bool expected_huffman) {
    return
        [expected_str, expected_huffman, this](
            const DecodeBuffer& input, DecodeStatus status) -> AssertionResult {
          AssertionResult result = Collected(expected_str, expected_huffman);
          if (result) {
            VERIFY_EQ(collector_,
                      HpackStringCollector(expected_str, expected_huffman));
          } else {
            VERIFY_NE(collector_,
                      HpackStringCollector(expected_str, expected_huffman));
          }
          HTTP2_VLOG(2) << collector_.ToString();
          collector_.Clear();
          HTTP2_VLOG(2) << collector_;
          return result;
        };
  }

  HpackStringDecoder decoder_;
  HpackStringCollector collector_;
  HpackStringDecoderVLoggingListener listener_;
  size_t start_decoding_calls_ = 0;
};

TEST_F(HpackStringDecoderTest, DecodeEmptyString) {
  {
    Validator validator = ValidateDoneAndEmpty(MakeValidator("", kCompressed));
    const char kData[] = {'\x80'};
    DecodeBuffer b(kData);
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, kMayReturnZeroOnFirst, validator));
  }
  {
    // Make sure it stops after decoding the empty string.
    Validator validator =
        ValidateDoneAndOffset(1, MakeValidator("", kUncompressed));
    const char kData[] = {'\x00', '\xff'};
    DecodeBuffer b(kData);
    EXPECT_EQ(2u, b.Remaining());
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, kMayReturnZeroOnFirst, validator));
    EXPECT_EQ(1u, b.Remaining());
  }
}

TEST_F(HpackStringDecoderTest, DecodeShortString) {
  {
    // Make sure it stops after decoding the non-empty string.
    Validator validator =
        ValidateDoneAndOffset(11, MakeValidator("start end.", kCompressed));
    const char kData[] = "\x8astart end.Don't peek at this.";
    DecodeBuffer b(kData);
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, kMayReturnZeroOnFirst, validator));
  }
  {
    Validator validator =
        ValidateDoneAndOffset(11, MakeValidator("start end.", kUncompressed));
    quiche::QuicheStringPiece data("\x0astart end.");
    DecodeBuffer b(data);
    EXPECT_TRUE(
        DecodeAndValidateSeveralWays(&b, kMayReturnZeroOnFirst, validator));
  }
}

TEST_F(HpackStringDecoderTest, DecodeLongStrings) {
  std::string name = Random().RandString(1024);
  std::string value = Random().RandString(65536);
  HpackBlockBuilder hbb;

  hbb.AppendString(false, name);
  uint32_t offset_after_name = hbb.size();
  EXPECT_EQ(3 + name.size(), offset_after_name);

  hbb.AppendString(true, value);
  uint32_t offset_after_value = hbb.size();
  EXPECT_EQ(3 + name.size() + 4 + value.size(), offset_after_value);

  DecodeBuffer b(hbb.buffer());

  // Decode the name...
  EXPECT_TRUE(DecodeAndValidateSeveralWays(
      &b, kMayReturnZeroOnFirst,
      ValidateDoneAndOffset(offset_after_name,
                            MakeValidator(name, kUncompressed))));
  EXPECT_EQ(offset_after_name, b.Offset());
  EXPECT_EQ(offset_after_value - offset_after_name, b.Remaining());

  // Decode the value...
  EXPECT_TRUE(DecodeAndValidateSeveralWays(
      &b, kMayReturnZeroOnFirst,
      ValidateDoneAndOffset(offset_after_value - offset_after_name,
                            MakeValidator(value, kCompressed))));
  EXPECT_EQ(offset_after_value, b.Offset());
  EXPECT_EQ(0u, b.Remaining());
}

}  // namespace
}  // namespace test
}  // namespace http2
