// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/huffman/hpack_huffman_decoder.h"

// Tests of HpackHuffmanDecoder and HuffmanBitBuffer.

#include <iostream>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"
#include "net/third_party/quiche/src/http2/decoder/decode_status.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

using ::testing::AssertionResult;

namespace http2 {
namespace test {
namespace {

TEST(HuffmanBitBufferTest, Reset) {
  HuffmanBitBuffer bb;
  EXPECT_TRUE(bb.IsEmpty());
  EXPECT_TRUE(bb.InputProperlyTerminated());
  EXPECT_EQ(bb.count(), 0u);
  EXPECT_EQ(bb.free_count(), 64u);
  EXPECT_EQ(bb.value(), 0u);
}

TEST(HuffmanBitBufferTest, AppendBytesAligned) {
  std::string s;
  s.push_back('\x11');
  s.push_back('\x22');
  s.push_back('\x33');
  quiche::QuicheStringPiece sp(s);

  HuffmanBitBuffer bb;
  sp.remove_prefix(bb.AppendBytes(sp));
  EXPECT_TRUE(sp.empty());
  EXPECT_FALSE(bb.IsEmpty()) << bb;
  EXPECT_FALSE(bb.InputProperlyTerminated());
  EXPECT_EQ(bb.count(), 24u) << bb;
  EXPECT_EQ(bb.free_count(), 40u) << bb;
  EXPECT_EQ(bb.value(), HuffmanAccumulator(0x112233) << 40) << bb;

  s.clear();
  s.push_back('\x44');
  sp = s;

  sp.remove_prefix(bb.AppendBytes(sp));
  EXPECT_TRUE(sp.empty());
  EXPECT_EQ(bb.count(), 32u) << bb;
  EXPECT_EQ(bb.free_count(), 32u) << bb;
  EXPECT_EQ(bb.value(), HuffmanAccumulator(0x11223344) << 32) << bb;

  s.clear();
  s.push_back('\x55');
  s.push_back('\x66');
  s.push_back('\x77');
  s.push_back('\x88');
  s.push_back('\x99');
  sp = s;

  sp.remove_prefix(bb.AppendBytes(sp));
  EXPECT_EQ(sp.size(), 1u);
  EXPECT_EQ('\x99', sp[0]);
  EXPECT_EQ(bb.count(), 64u) << bb;
  EXPECT_EQ(bb.free_count(), 0u) << bb;
  EXPECT_EQ(bb.value(), HuffmanAccumulator(0x1122334455667788LL)) << bb;

  sp.remove_prefix(bb.AppendBytes(sp));
  EXPECT_EQ(sp.size(), 1u);
  EXPECT_EQ('\x99', sp[0]);
  EXPECT_EQ(bb.count(), 64u) << bb;
  EXPECT_EQ(bb.free_count(), 0u) << bb;
  EXPECT_EQ(bb.value(), HuffmanAccumulator(0x1122334455667788LL)) << bb;
}

TEST(HuffmanBitBufferTest, ConsumeBits) {
  std::string s;
  s.push_back('\x11');
  s.push_back('\x22');
  s.push_back('\x33');
  quiche::QuicheStringPiece sp(s);

  HuffmanBitBuffer bb;
  sp.remove_prefix(bb.AppendBytes(sp));
  EXPECT_TRUE(sp.empty());

  bb.ConsumeBits(1);
  EXPECT_EQ(bb.count(), 23u) << bb;
  EXPECT_EQ(bb.free_count(), 41u) << bb;
  EXPECT_EQ(bb.value(), HuffmanAccumulator(0x112233) << 41) << bb;

  bb.ConsumeBits(20);
  EXPECT_EQ(bb.count(), 3u) << bb;
  EXPECT_EQ(bb.free_count(), 61u) << bb;
  EXPECT_EQ(bb.value(), HuffmanAccumulator(0x3) << 61) << bb;
}

TEST(HuffmanBitBufferTest, AppendBytesUnaligned) {
  std::string s;
  s.push_back('\x11');
  s.push_back('\x22');
  s.push_back('\x33');
  s.push_back('\x44');
  s.push_back('\x55');
  s.push_back('\x66');
  s.push_back('\x77');
  s.push_back('\x88');
  s.push_back('\x99');
  s.push_back('\xaa');
  s.push_back('\xbb');
  s.push_back('\xcc');
  s.push_back('\xdd');
  quiche::QuicheStringPiece sp(s);

  HuffmanBitBuffer bb;
  sp.remove_prefix(bb.AppendBytes(sp));
  EXPECT_EQ(sp.size(), 5u);
  EXPECT_FALSE(bb.InputProperlyTerminated());

  bb.ConsumeBits(15);
  EXPECT_EQ(bb.count(), 49u) << bb;
  EXPECT_EQ(bb.free_count(), 15u) << bb;

  HuffmanAccumulator expected(0x1122334455667788);
  expected <<= 15;
  EXPECT_EQ(bb.value(), expected);

  sp.remove_prefix(bb.AppendBytes(sp));
  EXPECT_EQ(sp.size(), 4u);
  EXPECT_EQ(bb.count(), 57u) << bb;
  EXPECT_EQ(bb.free_count(), 7u) << bb;

  expected |= (HuffmanAccumulator(0x99) << 7);
  EXPECT_EQ(bb.value(), expected)
      << bb << std::hex << "\n   actual: " << bb.value()
      << "\n expected: " << expected;
}

class HpackHuffmanDecoderTest : public RandomDecoderTest {
 protected:
  HpackHuffmanDecoderTest() {
    // The decoder may return true, and its accumulator may be empty, at
    // many boundaries while decoding, and yet the whole string hasn't
    // been decoded.
    stop_decode_on_done_ = false;
  }

  DecodeStatus StartDecoding(DecodeBuffer* b) override {
    input_bytes_seen_ = 0;
    output_buffer_.clear();
    decoder_.Reset();
    return ResumeDecoding(b);
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* b) override {
    input_bytes_seen_ += b->Remaining();
    quiche::QuicheStringPiece sp(b->cursor(), b->Remaining());
    if (decoder_.Decode(sp, &output_buffer_)) {
      b->AdvanceCursor(b->Remaining());
      // Successfully decoded (or buffered) the bytes in QuicheStringPiece.
      EXPECT_LE(input_bytes_seen_, input_bytes_expected_);
      // Have we reached the end of the encoded string?
      if (input_bytes_expected_ == input_bytes_seen_) {
        if (decoder_.InputProperlyTerminated()) {
          return DecodeStatus::kDecodeDone;
        } else {
          return DecodeStatus::kDecodeError;
        }
      }
      return DecodeStatus::kDecodeInProgress;
    }
    return DecodeStatus::kDecodeError;
  }

  HpackHuffmanDecoder decoder_;
  std::string output_buffer_;
  size_t input_bytes_seen_;
  size_t input_bytes_expected_;
};

TEST_F(HpackHuffmanDecoderTest, SpecRequestExamples) {
  HpackHuffmanDecoder decoder;
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
  for (size_t i = 0; i != QUICHE_ARRAYSIZE(test_table); i += 2) {
    const std::string& huffman_encoded(test_table[i]);
    const std::string& plain_string(test_table[i + 1]);
    std::string buffer;
    decoder.Reset();
    EXPECT_TRUE(decoder.Decode(huffman_encoded, &buffer)) << decoder;
    EXPECT_TRUE(decoder.InputProperlyTerminated()) << decoder;
    EXPECT_EQ(buffer, plain_string);
  }
}

TEST_F(HpackHuffmanDecoderTest, SpecResponseExamples) {
  HpackHuffmanDecoder decoder;
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
  for (size_t i = 0; i != QUICHE_ARRAYSIZE(test_table); i += 2) {
    const std::string& huffman_encoded(test_table[i]);
    const std::string& plain_string(test_table[i + 1]);
    std::string buffer;
    decoder.Reset();
    EXPECT_TRUE(decoder.Decode(huffman_encoded, &buffer)) << decoder;
    EXPECT_TRUE(decoder.InputProperlyTerminated()) << decoder;
    EXPECT_EQ(buffer, plain_string);
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
