// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/platform/api/quiche_text_utils.h"

#include <string>

#include "absl/strings/escaping.h"
#include "common/platform/api/quiche_test.h"

namespace quiche {
namespace test {

class QuicheTextUtilsTest : public QuicheTest {};

TEST_F(QuicheTextUtilsTest, ToLower) {
  EXPECT_EQ("lower", quiche::QuicheTextUtils::ToLower("LOWER"));
  EXPECT_EQ("lower", quiche::QuicheTextUtils::ToLower("lower"));
  EXPECT_EQ("lower", quiche::QuicheTextUtils::ToLower("lOwEr"));
  EXPECT_EQ("123", quiche::QuicheTextUtils::ToLower("123"));
  EXPECT_EQ("", quiche::QuicheTextUtils::ToLower(""));
}

TEST_F(QuicheTextUtilsTest, RemoveLeadingAndTrailingWhitespace) {
  std::string input;

  for (auto* input : {"text", " text", "  text", "text ", "text  ", " text ",
                      "  text  ", "\r\n\ttext", "text\n\r\t"}) {
    absl::string_view piece(input);
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&piece);
    EXPECT_EQ("text", piece);
  }
}

TEST_F(QuicheTextUtilsTest, Uint64ToString) {
  EXPECT_EQ("123", quiche::QuicheTextUtils::Uint64ToString(123));
  EXPECT_EQ("1234", quiche::QuicheTextUtils::Uint64ToString(1234));
}

TEST_F(QuicheTextUtilsTest, HexDump) {
  // Verify output of the HexDump method is as expected.
  char packet[] = {
      0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x2c, 0x20, 0x51, 0x55, 0x49, 0x43, 0x21,
      0x20, 0x54, 0x68, 0x69, 0x73, 0x20, 0x73, 0x74, 0x72, 0x69, 0x6e, 0x67,
      0x20, 0x73, 0x68, 0x6f, 0x75, 0x6c, 0x64, 0x20, 0x62, 0x65, 0x20, 0x6c,
      0x6f, 0x6e, 0x67, 0x20, 0x65, 0x6e, 0x6f, 0x75, 0x67, 0x68, 0x20, 0x74,
      0x6f, 0x20, 0x73, 0x70, 0x61, 0x6e, 0x20, 0x6d, 0x75, 0x6c, 0x74, 0x69,
      0x70, 0x6c, 0x65, 0x20, 0x6c, 0x69, 0x6e, 0x65, 0x73, 0x20, 0x6f, 0x66,
      0x20, 0x6f, 0x75, 0x74, 0x70, 0x75, 0x74, 0x2e, 0x01, 0x02, 0x03, 0x00,
  };
  EXPECT_EQ(
      quiche::QuicheTextUtils::HexDump(packet),
      "0x0000:  4865 6c6c 6f2c 2051 5549 4321 2054 6869  Hello,.QUIC!.Thi\n"
      "0x0010:  7320 7374 7269 6e67 2073 686f 756c 6420  s.string.should.\n"
      "0x0020:  6265 206c 6f6e 6720 656e 6f75 6768 2074  be.long.enough.t\n"
      "0x0030:  6f20 7370 616e 206d 756c 7469 706c 6520  o.span.multiple.\n"
      "0x0040:  6c69 6e65 7320 6f66 206f 7574 7075 742e  lines.of.output.\n"
      "0x0050:  0102 03                                  ...\n");
  // Verify that 0x21 and 0x7e are printable, 0x20 and 0x7f are not.
  EXPECT_EQ(
      "0x0000:  2021 7e7f                                .!~.\n",
      quiche::QuicheTextUtils::HexDump(absl::HexStringToBytes("20217e7f")));
  // Verify that values above numeric_limits<unsigned char>::max() are formatted
  // properly on platforms where char is unsigned.
  EXPECT_EQ("0x0000:  90aa ff                                  ...\n",
            quiche::QuicheTextUtils::HexDump(absl::HexStringToBytes("90aaff")));
}

TEST_F(QuicheTextUtilsTest, Base64Encode) {
  std::string output;
  std::string input = "Hello";
  quiche::QuicheTextUtils::Base64Encode(
      reinterpret_cast<const uint8_t*>(input.data()), input.length(), &output);
  EXPECT_EQ("SGVsbG8", output);

  input =
      "Hello, QUIC! This string should be long enough to span"
      "multiple lines of output\n";
  quiche::QuicheTextUtils::Base64Encode(
      reinterpret_cast<const uint8_t*>(input.data()), input.length(), &output);
  EXPECT_EQ(
      "SGVsbG8sIFFVSUMhIFRoaXMgc3RyaW5nIHNob3VsZCBiZSBsb25n"
      "IGVub3VnaCB0byBzcGFubXVsdGlwbGUgbGluZXMgb2Ygb3V0cHV0Cg",
      output);
}

TEST_F(QuicheTextUtilsTest, ContainsUpperCase) {
  EXPECT_FALSE(quiche::QuicheTextUtils::ContainsUpperCase("abc"));
  EXPECT_FALSE(quiche::QuicheTextUtils::ContainsUpperCase(""));
  EXPECT_FALSE(quiche::QuicheTextUtils::ContainsUpperCase("123"));
  EXPECT_TRUE(quiche::QuicheTextUtils::ContainsUpperCase("ABC"));
  EXPECT_TRUE(quiche::QuicheTextUtils::ContainsUpperCase("aBc"));
}

}  // namespace test
}  // namespace quiche
