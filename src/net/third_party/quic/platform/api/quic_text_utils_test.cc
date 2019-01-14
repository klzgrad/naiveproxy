// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_text_utils.h"

#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class QuicTextUtilsTest : public QuicTest {};

TEST_F(QuicTextUtilsTest, StartsWith) {
  EXPECT_TRUE(QuicTextUtils::StartsWith("hello world", "hello"));
  EXPECT_TRUE(QuicTextUtils::StartsWith("hello world", "hello world"));
  EXPECT_TRUE(QuicTextUtils::StartsWith("hello world", ""));
  EXPECT_FALSE(QuicTextUtils::StartsWith("hello world", "Hello"));
  EXPECT_FALSE(QuicTextUtils::StartsWith("hello world", "world"));
  EXPECT_FALSE(QuicTextUtils::StartsWith("hello world", "bar"));
}

TEST_F(QuicTextUtilsTest, EndsWithIgnoreCase) {
  EXPECT_TRUE(QuicTextUtils::EndsWithIgnoreCase("hello world", "world"));
  EXPECT_TRUE(QuicTextUtils::EndsWithIgnoreCase("hello world", "hello world"));
  EXPECT_TRUE(QuicTextUtils::EndsWithIgnoreCase("hello world", ""));
  EXPECT_TRUE(QuicTextUtils::EndsWithIgnoreCase("hello world", "WORLD"));
  EXPECT_FALSE(QuicTextUtils::EndsWithIgnoreCase("hello world", "hello"));
}

TEST_F(QuicTextUtilsTest, ToLower) {
  EXPECT_EQ("lower", QuicTextUtils::ToLower("LOWER"));
  EXPECT_EQ("lower", QuicTextUtils::ToLower("lower"));
  EXPECT_EQ("lower", QuicTextUtils::ToLower("lOwEr"));
  EXPECT_EQ("123", QuicTextUtils::ToLower("123"));
  EXPECT_EQ("", QuicTextUtils::ToLower(""));
}

TEST_F(QuicTextUtilsTest, RemoveLeadingAndTrailingWhitespace) {
  QuicString input;

  for (auto* input : {"text", " text", "  text", "text ", "text  ", " text ",
                      "  text  ", "\r\n\ttext", "text\n\r\t"}) {
    QuicStringPiece piece(input);
    QuicTextUtils::RemoveLeadingAndTrailingWhitespace(&piece);
    EXPECT_EQ("text", piece);
  }
}

TEST_F(QuicTextUtilsTest, StringToNumbers) {
  const QuicString kMaxInt32Plus1 = "2147483648";
  const QuicString kMinInt32Minus1 = "-2147483649";
  const QuicString kMaxUint32Plus1 = "4294967296";

  {
    // StringToUint64
    uint64_t uint64_val = 0;
    EXPECT_TRUE(QuicTextUtils::StringToUint64("123", &uint64_val));
    EXPECT_EQ(123u, uint64_val);
    EXPECT_TRUE(QuicTextUtils::StringToUint64("1234", &uint64_val));
    EXPECT_EQ(1234u, uint64_val);
    EXPECT_FALSE(QuicTextUtils::StringToUint64("", &uint64_val));
    EXPECT_FALSE(QuicTextUtils::StringToUint64("-123", &uint64_val));
    EXPECT_FALSE(QuicTextUtils::StringToUint64("-123.0", &uint64_val));
    EXPECT_TRUE(QuicTextUtils::StringToUint64(kMaxUint32Plus1, &uint64_val));
    EXPECT_EQ(4294967296u, uint64_val);
  }

  {
    // StringToint
    int int_val = 0;
    EXPECT_TRUE(QuicTextUtils::StringToInt("123", &int_val));
    EXPECT_EQ(123, int_val);
    EXPECT_TRUE(QuicTextUtils::StringToInt("1234", &int_val));
    EXPECT_EQ(1234, int_val);
    EXPECT_FALSE(QuicTextUtils::StringToInt("", &int_val));
    EXPECT_TRUE(QuicTextUtils::StringToInt("-123", &int_val));
    EXPECT_EQ(-123, int_val);
    EXPECT_FALSE(QuicTextUtils::StringToInt("-123.0", &int_val));
    if (sizeof(int) > 4) {
      EXPECT_TRUE(QuicTextUtils::StringToInt(kMinInt32Minus1, &int_val));
      EXPECT_EQ(-2147483649ll, int_val);
      EXPECT_TRUE(QuicTextUtils::StringToInt(kMaxInt32Plus1, &int_val));
      EXPECT_EQ(2147483648ll, int_val);
    } else {
      EXPECT_FALSE(QuicTextUtils::StringToInt(kMinInt32Minus1, &int_val));
      EXPECT_FALSE(QuicTextUtils::StringToInt(kMaxInt32Plus1, &int_val));
    }
  }

  {
    // StringToUint32
    uint32_t uint32_val = 0;
    EXPECT_TRUE(QuicTextUtils::StringToUint32("123", &uint32_val));
    EXPECT_EQ(123u, uint32_val);
    EXPECT_TRUE(QuicTextUtils::StringToUint32("1234", &uint32_val));
    EXPECT_EQ(1234u, uint32_val);
    EXPECT_FALSE(QuicTextUtils::StringToUint32("", &uint32_val));
    EXPECT_FALSE(QuicTextUtils::StringToUint32("-123", &uint32_val));
    EXPECT_FALSE(QuicTextUtils::StringToUint32("-123.0", &uint32_val));
    EXPECT_FALSE(QuicTextUtils::StringToUint32(kMaxUint32Plus1, &uint32_val));
  }

  {
    // StringToSizeT
    size_t size_t_val = 0;
    EXPECT_TRUE(QuicTextUtils::StringToSizeT("123", &size_t_val));
    EXPECT_EQ(123u, size_t_val);
    EXPECT_TRUE(QuicTextUtils::StringToSizeT("1234", &size_t_val));
    EXPECT_EQ(1234u, size_t_val);
    EXPECT_FALSE(QuicTextUtils::StringToSizeT("", &size_t_val));
    EXPECT_FALSE(QuicTextUtils::StringToSizeT("-123", &size_t_val));
    EXPECT_FALSE(QuicTextUtils::StringToSizeT("-123.0", &size_t_val));
    if (sizeof(size_t) > 4) {
      EXPECT_TRUE(QuicTextUtils::StringToSizeT(kMaxUint32Plus1, &size_t_val));
      EXPECT_EQ(4294967296ull, size_t_val);
    } else {
      EXPECT_FALSE(QuicTextUtils::StringToSizeT(kMaxUint32Plus1, &size_t_val));
    }
  }
}

TEST_F(QuicTextUtilsTest, Uint64ToString) {
  EXPECT_EQ("123", QuicTextUtils::Uint64ToString(123));
  EXPECT_EQ("1234", QuicTextUtils::Uint64ToString(1234));
}

TEST_F(QuicTextUtilsTest, HexEncode) {
  EXPECT_EQ("48656c6c6f", QuicTextUtils::HexEncode("Hello", 5));
  EXPECT_EQ("48656c6c6f", QuicTextUtils::HexEncode("Hello World", 5));
  EXPECT_EQ("48656c6c6f", QuicTextUtils::HexEncode("Hello"));
  EXPECT_EQ("0102779cfa", QuicTextUtils::HexEncode("\x01\x02\x77\x9c\xfa"));
}

TEST_F(QuicTextUtilsTest, HexDecode) {
  EXPECT_EQ("Hello", QuicTextUtils::HexDecode("48656c6c6f"));
  EXPECT_EQ("", QuicTextUtils::HexDecode(""));
  EXPECT_EQ("\x01\x02\x77\x9c\xfa", QuicTextUtils::HexDecode("0102779cfa"));
}

TEST_F(QuicTextUtilsTest, HexDump) {
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
      QuicTextUtils::HexDump(packet),
      "0x0000:  4865 6c6c 6f2c 2051 5549 4321 2054 6869  Hello,.QUIC!.Thi\n"
      "0x0010:  7320 7374 7269 6e67 2073 686f 756c 6420  s.string.should.\n"
      "0x0020:  6265 206c 6f6e 6720 656e 6f75 6768 2074  be.long.enough.t\n"
      "0x0030:  6f20 7370 616e 206d 756c 7469 706c 6520  o.span.multiple.\n"
      "0x0040:  6c69 6e65 7320 6f66 206f 7574 7075 742e  lines.of.output.\n"
      "0x0050:  0102 03                                  ...\n");
  // Verify that 0x21 and 0x7e are printable, 0x20 and 0x7f are not.
  EXPECT_EQ("0x0000:  2021 7e7f                                .!~.\n",
            QuicTextUtils::HexDump(QuicTextUtils::HexDecode("20217e7f")));
  // Verify that values above numeric_limits<unsigned char>::max() are formatted
  // properly on platforms where char is unsigned.
  EXPECT_EQ("0x0000:  90aa ff                                  ...\n",
            QuicTextUtils::HexDump(QuicTextUtils::HexDecode("90aaff")));
}

TEST_F(QuicTextUtilsTest, Base64Encode) {
  QuicString output;
  QuicString input = "Hello";
  QuicTextUtils::Base64Encode(reinterpret_cast<const uint8_t*>(input.data()),
                              input.length(), &output);
  EXPECT_EQ("SGVsbG8", output);

  input =
      "Hello, QUIC! This string should be long enough to span"
      "multiple lines of output\n";
  QuicTextUtils::Base64Encode(reinterpret_cast<const uint8_t*>(input.data()),
                              input.length(), &output);
  EXPECT_EQ(
      "SGVsbG8sIFFVSUMhIFRoaXMgc3RyaW5nIHNob3VsZCBiZSBsb25n"
      "IGVub3VnaCB0byBzcGFubXVsdGlwbGUgbGluZXMgb2Ygb3V0cHV0Cg",
      output);
}

TEST_F(QuicTextUtilsTest, ContainsUpperCase) {
  EXPECT_FALSE(QuicTextUtils::ContainsUpperCase("abc"));
  EXPECT_FALSE(QuicTextUtils::ContainsUpperCase(""));
  EXPECT_FALSE(QuicTextUtils::ContainsUpperCase("123"));
  EXPECT_TRUE(QuicTextUtils::ContainsUpperCase("ABC"));
  EXPECT_TRUE(QuicTextUtils::ContainsUpperCase("aBc"));
}

TEST_F(QuicTextUtilsTest, Split) {
  EXPECT_EQ(std::vector<QuicStringPiece>({"a", "b", "c"}),
            QuicTextUtils::Split("a,b,c", ','));
  EXPECT_EQ(std::vector<QuicStringPiece>({"a", "b", "c"}),
            QuicTextUtils::Split("a:b:c", ':'));
  EXPECT_EQ(std::vector<QuicStringPiece>({"a:b:c"}),
            QuicTextUtils::Split("a:b:c", ','));
}

}  // namespace test
}  // namespace quic
