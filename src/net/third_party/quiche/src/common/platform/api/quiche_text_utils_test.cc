// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

#include <string>

#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"

namespace quiche {
namespace test {

class QuicheTextUtilsTest : public QuicheTest {};

TEST_F(QuicheTextUtilsTest, StartsWith) {
  EXPECT_TRUE(quiche::QuicheTextUtils::StartsWith("hello world", "hello"));
  EXPECT_TRUE(
      quiche::QuicheTextUtils::StartsWith("hello world", "hello world"));
  EXPECT_TRUE(quiche::QuicheTextUtils::StartsWith("hello world", ""));
  EXPECT_FALSE(quiche::QuicheTextUtils::StartsWith("hello world", "Hello"));
  EXPECT_FALSE(quiche::QuicheTextUtils::StartsWith("hello world", "world"));
  EXPECT_FALSE(quiche::QuicheTextUtils::StartsWith("hello world", "bar"));
}

TEST_F(QuicheTextUtilsTest, EndsWithIgnoreCase) {
  EXPECT_TRUE(
      quiche::QuicheTextUtils::EndsWithIgnoreCase("hello world", "world"));
  EXPECT_TRUE(quiche::QuicheTextUtils::EndsWithIgnoreCase("hello world",
                                                          "hello world"));
  EXPECT_TRUE(quiche::QuicheTextUtils::EndsWithIgnoreCase("hello world", ""));
  EXPECT_TRUE(
      quiche::QuicheTextUtils::EndsWithIgnoreCase("hello world", "WORLD"));
  EXPECT_FALSE(
      quiche::QuicheTextUtils::EndsWithIgnoreCase("hello world", "hello"));
}

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
    quiche::QuicheStringPiece piece(input);
    quiche::QuicheTextUtils::RemoveLeadingAndTrailingWhitespace(&piece);
    EXPECT_EQ("text", piece);
  }
}

TEST_F(QuicheTextUtilsTest, StringToNumbers) {
  const std::string kMaxInt32Plus1 = "2147483648";
  const std::string kMinInt32Minus1 = "-2147483649";
  const std::string kMaxUint32Plus1 = "4294967296";

  {
    // StringToUint64
    uint64_t uint64_val = 0;
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToUint64("123", &uint64_val));
    EXPECT_EQ(123u, uint64_val);
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToUint64("1234", &uint64_val));
    EXPECT_EQ(1234u, uint64_val);
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToUint64("", &uint64_val));
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToUint64("-123", &uint64_val));
    EXPECT_FALSE(
        quiche::QuicheTextUtils::StringToUint64("-123.0", &uint64_val));
    EXPECT_TRUE(
        quiche::QuicheTextUtils::StringToUint64(kMaxUint32Plus1, &uint64_val));
    EXPECT_EQ(4294967296u, uint64_val);
  }

  {
    // StringToint
    int int_val = 0;
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToInt("123", &int_val));
    EXPECT_EQ(123, int_val);
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToInt("1234", &int_val));
    EXPECT_EQ(1234, int_val);
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToInt("", &int_val));
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToInt("-123", &int_val));
    EXPECT_EQ(-123, int_val);
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToInt("-123.0", &int_val));
    if (sizeof(int) > 4) {
      EXPECT_TRUE(
          quiche::QuicheTextUtils::StringToInt(kMinInt32Minus1, &int_val));
      EXPECT_EQ(-2147483649ll, int_val);
      EXPECT_TRUE(
          quiche::QuicheTextUtils::StringToInt(kMaxInt32Plus1, &int_val));
      EXPECT_EQ(2147483648ll, int_val);
    } else {
      EXPECT_FALSE(
          quiche::QuicheTextUtils::StringToInt(kMinInt32Minus1, &int_val));
      EXPECT_FALSE(
          quiche::QuicheTextUtils::StringToInt(kMaxInt32Plus1, &int_val));
    }
  }

  {
    // StringToUint32
    uint32_t uint32_val = 0;
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToUint32("123", &uint32_val));
    EXPECT_EQ(123u, uint32_val);
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToUint32("1234", &uint32_val));
    EXPECT_EQ(1234u, uint32_val);
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToUint32("", &uint32_val));
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToUint32("-123", &uint32_val));
    EXPECT_FALSE(
        quiche::QuicheTextUtils::StringToUint32("-123.0", &uint32_val));
    EXPECT_FALSE(
        quiche::QuicheTextUtils::StringToUint32(kMaxUint32Plus1, &uint32_val));
  }

  {
    // StringToSizeT
    size_t size_t_val = 0;
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToSizeT("123", &size_t_val));
    EXPECT_EQ(123u, size_t_val);
    EXPECT_TRUE(quiche::QuicheTextUtils::StringToSizeT("1234", &size_t_val));
    EXPECT_EQ(1234u, size_t_val);
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToSizeT("", &size_t_val));
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToSizeT("-123", &size_t_val));
    EXPECT_FALSE(quiche::QuicheTextUtils::StringToSizeT("-123.0", &size_t_val));
    if (sizeof(size_t) > 4) {
      EXPECT_TRUE(
          quiche::QuicheTextUtils::StringToSizeT(kMaxUint32Plus1, &size_t_val));
      EXPECT_EQ(4294967296ull, size_t_val);
    } else {
      EXPECT_FALSE(
          quiche::QuicheTextUtils::StringToSizeT(kMaxUint32Plus1, &size_t_val));
    }
  }
}

TEST_F(QuicheTextUtilsTest, Uint64ToString) {
  EXPECT_EQ("123", quiche::QuicheTextUtils::Uint64ToString(123));
  EXPECT_EQ("1234", quiche::QuicheTextUtils::Uint64ToString(1234));
}

TEST_F(QuicheTextUtilsTest, HexEncode) {
  EXPECT_EQ("48656c6c6f", quiche::QuicheTextUtils::HexEncode("Hello", 5));
  EXPECT_EQ("48656c6c6f", quiche::QuicheTextUtils::HexEncode("Hello World", 5));
  EXPECT_EQ("48656c6c6f", quiche::QuicheTextUtils::HexEncode("Hello"));
  EXPECT_EQ("0102779cfa",
            quiche::QuicheTextUtils::HexEncode("\x01\x02\x77\x9c\xfa"));
}

TEST_F(QuicheTextUtilsTest, HexDecode) {
  EXPECT_EQ("Hello", quiche::QuicheTextUtils::HexDecode("48656c6c6f"));
  EXPECT_EQ("", quiche::QuicheTextUtils::HexDecode(""));
  EXPECT_EQ("\x01\x02\x77\x9c\xfa",
            quiche::QuicheTextUtils::HexDecode("0102779cfa"));
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
  EXPECT_EQ("0x0000:  2021 7e7f                                .!~.\n",
            quiche::QuicheTextUtils::HexDump(
                quiche::QuicheTextUtils::HexDecode("20217e7f")));
  // Verify that values above numeric_limits<unsigned char>::max() are formatted
  // properly on platforms where char is unsigned.
  EXPECT_EQ("0x0000:  90aa ff                                  ...\n",
            quiche::QuicheTextUtils::HexDump(
                quiche::QuicheTextUtils::HexDecode("90aaff")));
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

TEST_F(QuicheTextUtilsTest, Split) {
  EXPECT_EQ(std::vector<quiche::QuicheStringPiece>({"a", "b", "c"}),
            quiche::QuicheTextUtils::Split("a,b,c", ','));
  EXPECT_EQ(std::vector<quiche::QuicheStringPiece>({"a", "b", "c"}),
            quiche::QuicheTextUtils::Split("a:b:c", ':'));
  EXPECT_EQ(std::vector<quiche::QuicheStringPiece>({"a:b:c"}),
            quiche::QuicheTextUtils::Split("a:b:c", ','));
  // Leading and trailing whitespace is preserved.
  EXPECT_EQ(std::vector<quiche::QuicheStringPiece>({"a", "b", "c"}),
            quiche::QuicheTextUtils::Split("a,b,c", ','));
  EXPECT_EQ(std::vector<quiche::QuicheStringPiece>({" a", "b ", " c "}),
            quiche::QuicheTextUtils::Split(" a:b : c ", ':'));
}

}  // namespace test
}  // namespace quiche
