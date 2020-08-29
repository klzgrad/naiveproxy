// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/platform/api/spdy_string_utils.h"

#include <cstdint>

#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"

namespace spdy {
namespace test {
namespace {

TEST(SpdyStringUtilsTest, SpdyStrAppend) {
  // No arguments on empty string.
  std::string output;
  SpdyStrAppend(&output);
  EXPECT_TRUE(output.empty());

  // Single string-like argument.
  const char kFoo[] = "foo";
  const std::string string_foo(kFoo);
  const quiche::QuicheStringPiece stringpiece_foo(string_foo);
  SpdyStrAppend(&output, kFoo);
  EXPECT_EQ("foo", output);
  SpdyStrAppend(&output, string_foo);
  EXPECT_EQ("foofoo", output);
  SpdyStrAppend(&output, stringpiece_foo);
  EXPECT_EQ("foofoofoo", output);

  // No arguments on non-empty string.
  SpdyStrAppend(&output);
  EXPECT_EQ("foofoofoo", output);

  output.clear();

  // Two string-like arguments.
  const char kBar[] = "bar";
  const quiche::QuicheStringPiece stringpiece_bar(kBar);
  const std::string string_bar(kBar);
  SpdyStrAppend(&output, kFoo, kBar);
  EXPECT_EQ("foobar", output);
  SpdyStrAppend(&output, kFoo, string_bar);
  EXPECT_EQ("foobarfoobar", output);
  SpdyStrAppend(&output, kFoo, stringpiece_bar);
  EXPECT_EQ("foobarfoobarfoobar", output);
  SpdyStrAppend(&output, string_foo, kBar);
  EXPECT_EQ("foobarfoobarfoobarfoobar", output);

  output.clear();

  SpdyStrAppend(&output, string_foo, string_bar);
  EXPECT_EQ("foobar", output);
  SpdyStrAppend(&output, string_foo, stringpiece_bar);
  EXPECT_EQ("foobarfoobar", output);
  SpdyStrAppend(&output, stringpiece_foo, kBar);
  EXPECT_EQ("foobarfoobarfoobar", output);
  SpdyStrAppend(&output, stringpiece_foo, string_bar);
  EXPECT_EQ("foobarfoobarfoobarfoobar", output);

  output.clear();

  SpdyStrAppend(&output, stringpiece_foo, stringpiece_bar);
  EXPECT_EQ("foobar", output);

  // Many-many arguments.
  SpdyStrAppend(&output, "foo", "bar", "baz", "qux", "quux", "quuz", "corge",
                "grault", "garply", "waldo", "fred", "plugh", "xyzzy", "thud");
  EXPECT_EQ(
      "foobarfoobarbazquxquuxquuzcorgegraultgarplywaldofredplughxyzzythud",
      output);

  output.clear();

  // Numerical arguments.
  const int16_t i = 1;
  const uint64_t u = 8;
  const double d = 3.1415;

  SpdyStrAppend(&output, i, " ", u);
  EXPECT_EQ("1 8", output);
  SpdyStrAppend(&output, d, i, i, u, i);
  EXPECT_EQ("1 83.14151181", output);
  SpdyStrAppend(&output, "i: ", i, ", u: ", u, ", d: ", d);
  EXPECT_EQ("1 83.14151181i: 1, u: 8, d: 3.1415", output);

  output.clear();

  // Boolean arguments.
  const bool t = true;
  const bool f = false;

  SpdyStrAppend(&output, t);
  EXPECT_EQ("1", output);
  SpdyStrAppend(&output, f);
  EXPECT_EQ("10", output);
  SpdyStrAppend(&output, f, t, t, f);
  EXPECT_EQ("100110", output);

  output.clear();

  // Mixed string-like, numerical, and Boolean arguments.
  SpdyStrAppend(&output, kFoo, i, string_foo, f, u, t, stringpiece_bar, d, t);
  EXPECT_EQ("foo1foo081bar3.14151", output);
  SpdyStrAppend(&output, d, t, t, string_bar, i, u, kBar, t, d, f);
  EXPECT_EQ("foo1foo081bar3.141513.141511bar18bar13.14150", output);
}

TEST(SpdyStringUtilsTest, SpdyHexDigitToInt) {
  EXPECT_EQ(0, SpdyHexDigitToInt('0'));
  EXPECT_EQ(1, SpdyHexDigitToInt('1'));
  EXPECT_EQ(2, SpdyHexDigitToInt('2'));
  EXPECT_EQ(3, SpdyHexDigitToInt('3'));
  EXPECT_EQ(4, SpdyHexDigitToInt('4'));
  EXPECT_EQ(5, SpdyHexDigitToInt('5'));
  EXPECT_EQ(6, SpdyHexDigitToInt('6'));
  EXPECT_EQ(7, SpdyHexDigitToInt('7'));
  EXPECT_EQ(8, SpdyHexDigitToInt('8'));
  EXPECT_EQ(9, SpdyHexDigitToInt('9'));

  EXPECT_EQ(10, SpdyHexDigitToInt('a'));
  EXPECT_EQ(11, SpdyHexDigitToInt('b'));
  EXPECT_EQ(12, SpdyHexDigitToInt('c'));
  EXPECT_EQ(13, SpdyHexDigitToInt('d'));
  EXPECT_EQ(14, SpdyHexDigitToInt('e'));
  EXPECT_EQ(15, SpdyHexDigitToInt('f'));

  EXPECT_EQ(10, SpdyHexDigitToInt('A'));
  EXPECT_EQ(11, SpdyHexDigitToInt('B'));
  EXPECT_EQ(12, SpdyHexDigitToInt('C'));
  EXPECT_EQ(13, SpdyHexDigitToInt('D'));
  EXPECT_EQ(14, SpdyHexDigitToInt('E'));
  EXPECT_EQ(15, SpdyHexDigitToInt('F'));
}

TEST(SpdyStringUtilsTest, SpdyHexDecodeToUInt32) {
  uint32_t out;
  EXPECT_TRUE(SpdyHexDecodeToUInt32("0", &out));
  EXPECT_EQ(0u, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("00", &out));
  EXPECT_EQ(0u, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("0000000", &out));
  EXPECT_EQ(0u, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("00000000", &out));
  EXPECT_EQ(0u, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("1", &out));
  EXPECT_EQ(1u, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("ffffFFF", &out));
  EXPECT_EQ(0xFFFFFFFu, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("fFfFffFf", &out));
  EXPECT_EQ(0xFFFFFFFFu, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("01AEF", &out));
  EXPECT_EQ(0x1AEFu, out);
  EXPECT_TRUE(SpdyHexDecodeToUInt32("abcde", &out));
  EXPECT_EQ(0xABCDEu, out);

  EXPECT_FALSE(SpdyHexDecodeToUInt32("", &out));
  EXPECT_FALSE(SpdyHexDecodeToUInt32("111111111", &out));
  EXPECT_FALSE(SpdyHexDecodeToUInt32("1111111111", &out));
  EXPECT_FALSE(SpdyHexDecodeToUInt32("0x1111", &out));
}

TEST(SpdyStringUtilsTest, SpdyHexEncode) {
  unsigned char bytes[] = {0x01, 0xff, 0x02, 0xfe, 0x03, 0x80, 0x81};
  EXPECT_EQ("01ff02fe038081",
            SpdyHexEncode(reinterpret_cast<char*>(bytes), sizeof(bytes)));
}

TEST(SpdyStringUtilsTest, SpdyHexEncodeUInt32AndTrim) {
  EXPECT_EQ("0", SpdyHexEncodeUInt32AndTrim(0));
  EXPECT_EQ("1", SpdyHexEncodeUInt32AndTrim(1));
  EXPECT_EQ("a", SpdyHexEncodeUInt32AndTrim(0xA));
  EXPECT_EQ("f", SpdyHexEncodeUInt32AndTrim(0xF));
  EXPECT_EQ("a9", SpdyHexEncodeUInt32AndTrim(0xA9));
  EXPECT_EQ("9abcdef", SpdyHexEncodeUInt32AndTrim(0x9ABCDEF));
  EXPECT_EQ("12345678", SpdyHexEncodeUInt32AndTrim(0x12345678));
  EXPECT_EQ("ffffffff", SpdyHexEncodeUInt32AndTrim(0xFFFFFFFF));
  EXPECT_EQ("10000001", SpdyHexEncodeUInt32AndTrim(0x10000001));
}

TEST(SpdyStringUtilsTest, SpdyStringPieceCaseHash) {
  SpdyStringPieceCaseHash hasher;
  auto mixed = hasher("To Be Or Not To Be, That is The Question");
  auto lower = hasher("to be or not to be, that is the question");
  EXPECT_EQ(mixed, lower);
  auto lower2 = hasher("to be or not to be, that is the question");
  EXPECT_EQ(lower, lower2);
  auto different = hasher("to see or not to see, that is the question");
  EXPECT_NE(lower, different);
  EXPECT_NE(lower, hasher(""));
}

TEST(SpdyStringUtilsTest, SpdyStringPieceCaseEq) {
  SpdyStringPieceCaseEq eq;
  EXPECT_TRUE(eq("To Be Or Not To Be, That is The Question",
                 "to be or not to be, that is the question"));
  EXPECT_TRUE(eq("to be or not to be, that is the question",
                 "to be or not to be, that is the question"));
  EXPECT_FALSE(eq("to be or not to be, that is the question",
                  "to see or not to see, that is the question"));
}

}  // namespace
}  // namespace test
}  // namespace spdy
