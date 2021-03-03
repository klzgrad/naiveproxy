// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http2/hpack/huffman/hpack_huffman_encoder.h"

#include "absl/base/macros.h"
#include "http2/platform/api/http2_string_utils.h"
#include "common/platform/api/quiche_test.h"

namespace http2 {
namespace {

class HuffmanEncoderTest : public ::testing::TestWithParam<bool> {
 protected:
  HuffmanEncoderTest() : use_fast_encoder_(GetParam()) {}
  virtual ~HuffmanEncoderTest() = default;

  void Encode(absl::string_view input,
              size_t encoded_size,
              std::string* output) {
    use_fast_encoder_ ? HuffmanEncodeFast(input, encoded_size, output)
                      : HuffmanEncode(input, encoded_size, output);
  }

  const bool use_fast_encoder_;
};

INSTANTIATE_TEST_SUITE_P(TwoEncoders, HuffmanEncoderTest, ::testing::Bool());

TEST_P(HuffmanEncoderTest, Empty) {
  std::string empty("");
  size_t encoded_size = HuffmanSize(empty);
  EXPECT_EQ(0u, encoded_size);

  std::string buffer;
  Encode(empty, encoded_size, &buffer);
  EXPECT_EQ("", buffer);
}

TEST_P(HuffmanEncoderTest, SpecRequestExamples) {
  std::string test_table[] = {
      Http2HexDecode("f1e3c2e5f23a6ba0ab90f4ff"),
      "www.example.com",
      Http2HexDecode("a8eb10649cbf"),
      "no-cache",
      Http2HexDecode("25a849e95ba97d7f"),
      "custom-key",
      Http2HexDecode("25a849e95bb8e8b4bf"),
      "custom-value",
  };
  for (size_t i = 0; i != ABSL_ARRAYSIZE(test_table); i += 2) {
    const std::string& huffman_encoded(test_table[i]);
    const std::string& plain_string(test_table[i + 1]);
    size_t encoded_size = HuffmanSize(plain_string);
    EXPECT_EQ(huffman_encoded.size(), encoded_size);
    std::string buffer;
    buffer.reserve();
    Encode(plain_string, encoded_size, &buffer);
    EXPECT_EQ(buffer, huffman_encoded) << "Error encoding " << plain_string;
  }
}

TEST_P(HuffmanEncoderTest, SpecResponseExamples) {
  // clang-format off
  std::string test_table[] = {
    Http2HexDecode("6402"),
    "302",
    Http2HexDecode("aec3771a4b"),
    "private",
    Http2HexDecode("d07abe941054d444a8200595040b8166"
            "e082a62d1bff"),
    "Mon, 21 Oct 2013 20:13:21 GMT",
    Http2HexDecode("9d29ad171863c78f0b97c8e9ae82ae43"
            "d3"),
    "https://www.example.com",
    Http2HexDecode("94e7821dd7f2e6c7b335dfdfcd5b3960"
            "d5af27087f3672c1ab270fb5291f9587"
            "316065c003ed4ee5b1063d5007"),
    "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
  };
  // clang-format on
  for (size_t i = 0; i != ABSL_ARRAYSIZE(test_table); i += 2) {
    const std::string& huffman_encoded(test_table[i]);
    const std::string& plain_string(test_table[i + 1]);
    size_t encoded_size = HuffmanSize(plain_string);
    EXPECT_EQ(huffman_encoded.size(), encoded_size);
    std::string buffer;
    Encode(plain_string, encoded_size, &buffer);
    EXPECT_EQ(buffer, huffman_encoded) << "Error encoding " << plain_string;
  }
}

TEST_P(HuffmanEncoderTest, EncodedSizeAgreesWithEncodeString) {
  std::string test_table[] = {
      "",
      "Mon, 21 Oct 2013 20:13:21 GMT",
      "https://www.example.com",
      "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
      std::string(1, '\0'),
      std::string("foo\0bar", 7),
      std::string(256, '\0'),
  };
  // Modify last |test_table| entry to cover all codes.
  for (size_t i = 0; i != 256; ++i) {
    test_table[ABSL_ARRAYSIZE(test_table) - 1][i] = static_cast<char>(i);
  }

  for (size_t i = 0; i != ABSL_ARRAYSIZE(test_table); ++i) {
    const std::string& plain_string = test_table[i];
    size_t encoded_size = HuffmanSize(plain_string);
    std::string huffman_encoded;
    Encode(plain_string, encoded_size, &huffman_encoded);
    EXPECT_EQ(encoded_size, huffman_encoded.size());
  }
}

// Test that encoding appends to output without overwriting it.
TEST_P(HuffmanEncoderTest, AppendToOutput) {
  size_t encoded_size = HuffmanSize("foo");
  std::string buffer;
  Encode("foo", encoded_size, &buffer);
  EXPECT_EQ(Http2HexDecode("94e7"), buffer);

  encoded_size = HuffmanSize("bar");
  Encode("bar", encoded_size, &buffer);
  EXPECT_EQ(Http2HexDecode("94e78c767f"), buffer);
}

}  // namespace
}  // namespace http2
