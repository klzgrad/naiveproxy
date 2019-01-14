// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/spdy/core/hpack/hpack_huffman_table.h"

#include <stdint.h>

#include <bitset>
#include <utility>

#include "base/logging.h"
#include "base/macros.h"
#include "net/third_party/http2/hpack/huffman/hpack_huffman_decoder.h"
#include "net/third_party/spdy/core/hpack/hpack_constants.h"
#include "net/third_party/spdy/core/hpack/hpack_output_stream.h"
#include "net/third_party/spdy/platform/api/spdy_string_utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace spdy {

namespace test {

class HpackHuffmanTablePeer {
 public:
  explicit HpackHuffmanTablePeer(const HpackHuffmanTable& table)
      : table_(table) {}

  const std::vector<uint32_t>& code_by_id() const { return table_.code_by_id_; }
  const std::vector<uint8_t>& length_by_id() const {
    return table_.length_by_id_;
  }
  char pad_bits() const {
    // Cast to match signed-ness of bits8().
    return static_cast<char>(table_.pad_bits_);
  }
  uint16_t failed_symbol_id() const { return table_.failed_symbol_id_; }

 private:
  const HpackHuffmanTable& table_;
};

namespace {

// Tests of the ability to encode some canonical Huffman code,
// not just the one defined in the RFC 7541.
class GenericHuffmanTableTest : public ::testing::Test {
 protected:
  GenericHuffmanTableTest() : table_(), peer_(table_) {}

  SpdyString EncodeString(SpdyStringPiece input) {
    SpdyString result;
    HpackOutputStream output_stream;
    table_.EncodeString(input, &output_stream);

    output_stream.TakeString(&result);
    // Verify EncodedSize() agrees with EncodeString().
    EXPECT_EQ(result.size(), table_.EncodedSize(input));
    return result;
  }

  HpackHuffmanTable table_;
  HpackHuffmanTablePeer peer_;
};

uint32_t bits32(const SpdyString& bitstring) {
  return std::bitset<32>(bitstring).to_ulong();
}
char bits8(const SpdyString& bitstring) {
  return static_cast<char>(std::bitset<8>(bitstring).to_ulong());
}

TEST_F(GenericHuffmanTableTest, InitializeEdgeCases) {
  {
    // Verify eight symbols can be encoded with 3 bits per symbol.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 3, 0},
        {bits32("00100000000000000000000000000000"), 3, 1},
        {bits32("01000000000000000000000000000000"), 3, 2},
        {bits32("01100000000000000000000000000000"), 3, 3},
        {bits32("10000000000000000000000000000000"), 3, 4},
        {bits32("10100000000000000000000000000000"), 3, 5},
        {bits32("11000000000000000000000000000000"), 3, 6},
        {bits32("11100000000000000000000000000000"), 8, 7}};
    HpackHuffmanTable table;
    EXPECT_TRUE(table.Initialize(code, arraysize(code)));
  }
  {
    // But using 2 bits with one symbol overflows the code.
    HpackHuffmanSymbol code[] = {
        {bits32("01000000000000000000000000000000"), 3, 0},
        {bits32("01100000000000000000000000000000"), 3, 1},
        {bits32("00000000000000000000000000000000"), 2, 2},
        {bits32("10000000000000000000000000000000"), 3, 3},
        {bits32("10100000000000000000000000000000"), 3, 4},
        {bits32("11000000000000000000000000000000"), 3, 5},
        {bits32("11100000000000000000000000000000"), 3, 6},
        {bits32("00000000000000000000000000000000"), 8, 7}};  // Overflow.
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(7, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Verify four symbols can be encoded with incremental bits per symbol.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 3, 2},
        {bits32("11100000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_TRUE(table.Initialize(code, arraysize(code)));
  }
  {
    // But repeating a length overflows the code.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 2, 2},
        {bits32("00000000000000000000000000000000"), 8, 3}};  // Overflow.
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(3, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Symbol IDs must be assigned sequentially with no gaps.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 3, 1},  // Repeat.
        {bits32("11100000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(2, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Canonical codes must begin with zero.
    HpackHuffmanSymbol code[] = {
        {bits32("10000000000000000000000000000000"), 4, 0},
        {bits32("10010000000000000000000000000000"), 4, 1},
        {bits32("10100000000000000000000000000000"), 4, 2},
        {bits32("10110000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(0, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // Codes must match the expected canonical sequence.
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 2, 0},
        {bits32("01000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 2, 2},  // Not canonical.
        {bits32("10000000000000000000000000000000"), 8, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
    EXPECT_EQ(2, HpackHuffmanTablePeer(table).failed_symbol_id());
  }
  {
    // At least one code must have a length of 8 bits (to ensure pad-ability).
    HpackHuffmanSymbol code[] = {
        {bits32("00000000000000000000000000000000"), 1, 0},
        {bits32("10000000000000000000000000000000"), 2, 1},
        {bits32("11000000000000000000000000000000"), 3, 2},
        {bits32("11100000000000000000000000000000"), 7, 3}};
    HpackHuffmanTable table;
    EXPECT_FALSE(table.Initialize(code, arraysize(code)));
  }
}

TEST_F(GenericHuffmanTableTest, ValidateInternalsWithSmallCode) {
  HpackHuffmanSymbol code[] = {
      {bits32("01100000000000000000000000000000"), 4, 0},  // 3rd.
      {bits32("01110000000000000000000000000000"), 4, 1},  // 4th.
      {bits32("00000000000000000000000000000000"), 2, 2},  // 1st assigned code.
      {bits32("01000000000000000000000000000000"), 3, 3},  // 2nd.
      {bits32("10000000000000000000000000000000"), 5, 4},  // 5th.
      {bits32("10001000000000000000000000000000"), 5, 5},  // 6th.
      {bits32("10011000000000000000000000000000"), 8, 6},  // 8th.
      {bits32("10010000000000000000000000000000"), 5, 7}};  // 7th.
  EXPECT_TRUE(table_.Initialize(code, arraysize(code)));
  ASSERT_EQ(arraysize(code), peer_.code_by_id().size());
  ASSERT_EQ(arraysize(code), peer_.length_by_id().size());
  for (size_t i = 0; i < arraysize(code); ++i) {
    EXPECT_EQ(code[i].code, peer_.code_by_id()[i]);
    EXPECT_EQ(code[i].length, peer_.length_by_id()[i]);
  }

  EXPECT_EQ(bits8("10011000"), peer_.pad_bits());

  char input_storage[] = {2, 3, 2, 7, 4};
  SpdyStringPiece input(input_storage, arraysize(input_storage));
  // By symbol: (2) 00 (3) 010 (2) 00 (7) 10010 (4) 10000 (6 as pad) 1001100.
  char expect_storage[] = {bits8("00010001"), bits8("00101000"),
                           bits8("01001100")};
  SpdyStringPiece expect(expect_storage, arraysize(expect_storage));
  EXPECT_EQ(expect, EncodeString(input));
}

// Tests of the ability to encode the HPACK Huffman Code, defined in:
//     https://httpwg.github.io/specs/rfc7541.html#huffman.code
class HpackHuffmanTableTest : public GenericHuffmanTableTest {
 protected:
  void SetUp() override {
    std::vector<HpackHuffmanSymbol> code = HpackHuffmanCode();
    EXPECT_TRUE(table_.Initialize(&code[0], code.size()));
    EXPECT_TRUE(table_.IsInitialized());
  }

  // Use http2::HpackHuffmanDecoder for roundtrip tests.
  void DecodeString(const SpdyString& encoded, SpdyString* out) {
    http2::HpackHuffmanDecoder decoder;
    out->clear();
    EXPECT_TRUE(decoder.Decode(encoded, out));
  }
};

TEST_F(HpackHuffmanTableTest, InitializeHpackCode) {
  EXPECT_EQ(peer_.pad_bits(), '\xFF');  // First 8 bits of EOS.
}

TEST_F(HpackHuffmanTableTest, SpecRequestExamples) {
  SpdyString buffer;
  SpdyString test_table[] = {
      SpdyHexDecode("f1e3c2e5f23a6ba0ab90f4ff"),
      "www.example.com",
      SpdyHexDecode("a8eb10649cbf"),
      "no-cache",
      SpdyHexDecode("25a849e95ba97d7f"),
      "custom-key",
      SpdyHexDecode("25a849e95bb8e8b4bf"),
      "custom-value",
  };
  // Round-trip each test example.
  for (size_t i = 0; i != arraysize(test_table); i += 2) {
    const SpdyString& encodedFixture(test_table[i]);
    const SpdyString& decodedFixture(test_table[i + 1]);
    DecodeString(encodedFixture, &buffer);
    EXPECT_EQ(decodedFixture, buffer);
    buffer = EncodeString(decodedFixture);
    EXPECT_EQ(encodedFixture, buffer);
  }
}

TEST_F(HpackHuffmanTableTest, SpecResponseExamples) {
  SpdyString buffer;
  SpdyString test_table[] = {
      SpdyHexDecode("6402"),
      "302",
      SpdyHexDecode("aec3771a4b"),
      "private",
      SpdyHexDecode("d07abe941054d444a8200595040b8166"
                    "e082a62d1bff"),
      "Mon, 21 Oct 2013 20:13:21 GMT",
      SpdyHexDecode("9d29ad171863c78f0b97c8e9ae82ae43"
                    "d3"),
      "https://www.example.com",
      SpdyHexDecode("94e7821dd7f2e6c7b335dfdfcd5b3960"
                    "d5af27087f3672c1ab270fb5291f9587"
                    "316065c003ed4ee5b1063d5007"),
      "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
  };
  // Round-trip each test example.
  for (size_t i = 0; i != arraysize(test_table); i += 2) {
    const SpdyString& encodedFixture(test_table[i]);
    const SpdyString& decodedFixture(test_table[i + 1]);
    DecodeString(encodedFixture, &buffer);
    EXPECT_EQ(decodedFixture, buffer);
    buffer = EncodeString(decodedFixture);
    EXPECT_EQ(encodedFixture, buffer);
  }
}

TEST_F(HpackHuffmanTableTest, RoundTripIndividualSymbols) {
  for (size_t i = 0; i != 256; i++) {
    char c = static_cast<char>(i);
    char storage[3] = {c, c, c};
    SpdyStringPiece input(storage, arraysize(storage));
    SpdyString buffer_in = EncodeString(input);
    SpdyString buffer_out;
    DecodeString(buffer_in, &buffer_out);
    EXPECT_EQ(input, buffer_out);
  }
}

TEST_F(HpackHuffmanTableTest, RoundTripSymbolSequence) {
  char storage[512];
  for (size_t i = 0; i != 256; i++) {
    storage[i] = static_cast<char>(i);
    storage[511 - i] = static_cast<char>(i);
  }
  SpdyStringPiece input(storage, arraysize(storage));

  SpdyString buffer_in = EncodeString(input);
  SpdyString buffer_out;
  DecodeString(buffer_in, &buffer_out);
  EXPECT_EQ(input, buffer_out);
}

TEST_F(HpackHuffmanTableTest, EncodedSizeAgreesWithEncodeString) {
  SpdyString test_table[] = {
      "",
      "Mon, 21 Oct 2013 20:13:21 GMT",
      "https://www.example.com",
      "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1",
      SpdyString(1, '\0'),
      SpdyString("foo\0bar", 7),
      SpdyString(256, '\0'),
  };
  for (size_t i = 0; i != 256; ++i) {
    // Expand last |test_table| entry to cover all codes.
    test_table[arraysize(test_table) - 1][i] = static_cast<char>(i);
  }

  HpackOutputStream output_stream;
  SpdyString encoding;
  for (size_t i = 0; i != arraysize(test_table); ++i) {
    table_.EncodeString(test_table[i], &output_stream);
    output_stream.TakeString(&encoding);
    EXPECT_EQ(encoding.size(), table_.EncodedSize(test_table[i]));
  }
}

}  // namespace

}  // namespace test

}  // namespace spdy
