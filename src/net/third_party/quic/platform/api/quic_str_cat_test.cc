// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_str_cat.h"

#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicStrCatTest : public QuicTest {};

TEST_F(QuicStrCatTest, Ints) {
  const int16_t s = -1;
  const uint16_t us = 2;
  const int i = -3;
  const uint32_t ui = 4;
  const int64_t l = -5;
  const uint64_t ul = 6;
  const ptrdiff_t ptrdiff = -7;
  const size_t size = 8;
  const intptr_t intptr = -9;
  const uintptr_t uintptr = 10;
  QuicString answer;
  answer = QuicStrCat(s, us);
  EXPECT_EQ(answer, "-12");
  answer = QuicStrCat(i, ui);
  EXPECT_EQ(answer, "-34");
  answer = QuicStrCat(l, ul);
  EXPECT_EQ(answer, "-56");
  answer = QuicStrCat(ptrdiff, size);
  EXPECT_EQ(answer, "-78");
  answer = QuicStrCat(size, intptr);
  EXPECT_EQ(answer, "8-9");
  answer = QuicStrCat(uintptr, 0);
  EXPECT_EQ(answer, "100");
}

TEST_F(QuicStrCatTest, Basics) {
  QuicString result;

  QuicString strs[] = {"Hello", "Cruel", "World"};

  QuicStringPiece pieces[] = {"Hello", "Cruel", "World"};

  const char* c_strs[] = {"Hello", "Cruel", "World"};

  int32_t i32s[] = {'H', 'C', 'W'};
  uint64_t ui64s[] = {12345678910LL, 10987654321LL};

  result = QuicStrCat(false, true, 2, 3);
  EXPECT_EQ(result, "0123");

  result = QuicStrCat(-1);
  EXPECT_EQ(result, "-1");

  result = QuicStrCat(0.5);
  EXPECT_EQ(result, "0.5");

  result = QuicStrCat(strs[1], pieces[2]);
  EXPECT_EQ(result, "CruelWorld");

  result = QuicStrCat(strs[0], ", ", pieces[2]);
  EXPECT_EQ(result, "Hello, World");

  result = QuicStrCat(strs[0], ", ", strs[1], " ", strs[2], "!");
  EXPECT_EQ(result, "Hello, Cruel World!");

  result = QuicStrCat(pieces[0], ", ", pieces[1], " ", pieces[2]);
  EXPECT_EQ(result, "Hello, Cruel World");

  result = QuicStrCat(c_strs[0], ", ", c_strs[1], " ", c_strs[2]);
  EXPECT_EQ(result, "Hello, Cruel World");

  result = QuicStrCat("ASCII ", i32s[0], ", ", i32s[1], " ", i32s[2], "!");
  EXPECT_EQ(result, "ASCII 72, 67 87!");

  result = QuicStrCat(ui64s[0], ", ", ui64s[1], "!");
  EXPECT_EQ(result, "12345678910, 10987654321!");

  QuicString one = "1";
  result = QuicStrCat("And a ", one.size(), " and a ", &result[2] - &result[0],
                      " and a ", one, " 2 3 4", "!");
  EXPECT_EQ(result, "And a 1 and a 2 and a 1 2 3 4!");

  result =
      QuicStrCat("To output a char by ASCII/numeric value, use +: ", '!' + 0);
  EXPECT_EQ(result, "To output a char by ASCII/numeric value, use +: 33");

  float f = 10000.5;
  result = QuicStrCat("Ten K and a half is ", f);
  EXPECT_EQ(result, "Ten K and a half is 10000.5");

  double d = 99999.9;
  result = QuicStrCat("This double number is ", d);
  EXPECT_EQ(result, "This double number is 99999.9");

  result =
      QuicStrCat(1, 22, 333, 4444, 55555, 666666, 7777777, 88888888, 999999999);
  EXPECT_EQ(result, "122333444455555666666777777788888888999999999");
}

TEST_F(QuicStrCatTest, MaxArgs) {
  QuicString result;
  // Test 10 up to 26 arguments, the current maximum
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a");
  EXPECT_EQ(result, "123456789a");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b");
  EXPECT_EQ(result, "123456789ab");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c");
  EXPECT_EQ(result, "123456789abc");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d");
  EXPECT_EQ(result, "123456789abcd");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e");
  EXPECT_EQ(result, "123456789abcde");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f");
  EXPECT_EQ(result, "123456789abcdef");
  result =
      QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f", "g");
  EXPECT_EQ(result, "123456789abcdefg");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h");
  EXPECT_EQ(result, "123456789abcdefgh");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i");
  EXPECT_EQ(result, "123456789abcdefghi");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j");
  EXPECT_EQ(result, "123456789abcdefghij");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k");
  EXPECT_EQ(result, "123456789abcdefghijk");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l");
  EXPECT_EQ(result, "123456789abcdefghijkl");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m");
  EXPECT_EQ(result, "123456789abcdefghijklm");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n");
  EXPECT_EQ(result, "123456789abcdefghijklmn");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n", "o");
  EXPECT_EQ(result, "123456789abcdefghijklmno");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n", "o", "p");
  EXPECT_EQ(result, "123456789abcdefghijklmnop");
  result = QuicStrCat(1, 2, 3, 4, 5, 6, 7, 8, 9, "a", "b", "c", "d", "e", "f",
                      "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q");
  EXPECT_EQ(result, "123456789abcdefghijklmnopq");
  // No limit thanks to C++11's variadic templates
  result = QuicStrCat(
      1, 2, 3, 4, 5, 6, 7, 8, 9, 10, "a", "b", "c", "d", "e", "f", "g", "h",
      "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w",
      "x", "y", "z", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
      "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z");
  EXPECT_EQ(result,
            "12345678910abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

}  // namespace
}  // namespace test
}  // namespace quic
