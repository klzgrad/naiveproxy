// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/platform/api/http2_string_utils.h"

#include <cstdint>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_string_piece.h"

namespace http2 {
namespace test {
namespace {

TEST(Http2StringUtilsTest, Http2StrCat) {
  // No arguments.
  EXPECT_EQ("", Http2StrCat());

  // Single string-like argument.
  const char kFoo[] = "foo";
  const std::string string_foo(kFoo);
  const Http2StringPiece stringpiece_foo(string_foo);
  EXPECT_EQ("foo", Http2StrCat(kFoo));
  EXPECT_EQ("foo", Http2StrCat(string_foo));
  EXPECT_EQ("foo", Http2StrCat(stringpiece_foo));

  // Two string-like arguments.
  const char kBar[] = "bar";
  const Http2StringPiece stringpiece_bar(kBar);
  const std::string string_bar(kBar);
  EXPECT_EQ("foobar", Http2StrCat(kFoo, kBar));
  EXPECT_EQ("foobar", Http2StrCat(kFoo, string_bar));
  EXPECT_EQ("foobar", Http2StrCat(kFoo, stringpiece_bar));
  EXPECT_EQ("foobar", Http2StrCat(string_foo, kBar));
  EXPECT_EQ("foobar", Http2StrCat(string_foo, string_bar));
  EXPECT_EQ("foobar", Http2StrCat(string_foo, stringpiece_bar));
  EXPECT_EQ("foobar", Http2StrCat(stringpiece_foo, kBar));
  EXPECT_EQ("foobar", Http2StrCat(stringpiece_foo, string_bar));
  EXPECT_EQ("foobar", Http2StrCat(stringpiece_foo, stringpiece_bar));

  // Many-many arguments.
  EXPECT_EQ(
      "foobarbazquxquuxquuzcorgegraultgarplywaldofredplughxyzzythud",
      Http2StrCat("foo", "bar", "baz", "qux", "quux", "quuz", "corge", "grault",
                  "garply", "waldo", "fred", "plugh", "xyzzy", "thud"));

  // Numerical arguments.
  const int16_t i = 1;
  const uint64_t u = 8;
  const double d = 3.1415;

  EXPECT_EQ("1 8", Http2StrCat(i, " ", u));
  EXPECT_EQ("3.14151181", Http2StrCat(d, i, i, u, i));
  EXPECT_EQ("i: 1, u: 8, d: 3.1415",
            Http2StrCat("i: ", i, ", u: ", u, ", d: ", d));

  // Boolean arguments.
  const bool t = true;
  const bool f = false;

  EXPECT_EQ("1", Http2StrCat(t));
  EXPECT_EQ("0", Http2StrCat(f));
  EXPECT_EQ("0110", Http2StrCat(f, t, t, f));

  // Mixed string-like, numerical, and Boolean arguments.
  EXPECT_EQ("foo1foo081bar3.14151",
            Http2StrCat(kFoo, i, string_foo, f, u, t, stringpiece_bar, d, t));
  EXPECT_EQ("3.141511bar18bar13.14150",
            Http2StrCat(d, t, t, string_bar, i, u, kBar, t, d, f));
}

TEST(Http2StringUtilsTest, Http2StrAppend) {
  // No arguments on empty string.
  std::string output;
  Http2StrAppend(&output);
  EXPECT_TRUE(output.empty());

  // Single string-like argument.
  const char kFoo[] = "foo";
  const std::string string_foo(kFoo);
  const Http2StringPiece stringpiece_foo(string_foo);
  Http2StrAppend(&output, kFoo);
  EXPECT_EQ("foo", output);
  Http2StrAppend(&output, string_foo);
  EXPECT_EQ("foofoo", output);
  Http2StrAppend(&output, stringpiece_foo);
  EXPECT_EQ("foofoofoo", output);

  // No arguments on non-empty string.
  Http2StrAppend(&output);
  EXPECT_EQ("foofoofoo", output);

  output.clear();

  // Two string-like arguments.
  const char kBar[] = "bar";
  const Http2StringPiece stringpiece_bar(kBar);
  const std::string string_bar(kBar);
  Http2StrAppend(&output, kFoo, kBar);
  EXPECT_EQ("foobar", output);
  Http2StrAppend(&output, kFoo, string_bar);
  EXPECT_EQ("foobarfoobar", output);
  Http2StrAppend(&output, kFoo, stringpiece_bar);
  EXPECT_EQ("foobarfoobarfoobar", output);
  Http2StrAppend(&output, string_foo, kBar);
  EXPECT_EQ("foobarfoobarfoobarfoobar", output);

  output.clear();

  Http2StrAppend(&output, string_foo, string_bar);
  EXPECT_EQ("foobar", output);
  Http2StrAppend(&output, string_foo, stringpiece_bar);
  EXPECT_EQ("foobarfoobar", output);
  Http2StrAppend(&output, stringpiece_foo, kBar);
  EXPECT_EQ("foobarfoobarfoobar", output);
  Http2StrAppend(&output, stringpiece_foo, string_bar);
  EXPECT_EQ("foobarfoobarfoobarfoobar", output);

  output.clear();

  Http2StrAppend(&output, stringpiece_foo, stringpiece_bar);
  EXPECT_EQ("foobar", output);

  // Many-many arguments.
  Http2StrAppend(&output, "foo", "bar", "baz", "qux", "quux", "quuz", "corge",
                 "grault", "garply", "waldo", "fred", "plugh", "xyzzy", "thud");
  EXPECT_EQ(
      "foobarfoobarbazquxquuxquuzcorgegraultgarplywaldofredplughxyzzythud",
      output);

  output.clear();

  // Numerical arguments.
  const int16_t i = 1;
  const uint64_t u = 8;
  const double d = 3.1415;

  Http2StrAppend(&output, i, " ", u);
  EXPECT_EQ("1 8", output);
  Http2StrAppend(&output, d, i, i, u, i);
  EXPECT_EQ("1 83.14151181", output);
  Http2StrAppend(&output, "i: ", i, ", u: ", u, ", d: ", d);
  EXPECT_EQ("1 83.14151181i: 1, u: 8, d: 3.1415", output);

  output.clear();

  // Boolean arguments.
  const bool t = true;
  const bool f = false;

  Http2StrAppend(&output, t);
  EXPECT_EQ("1", output);
  Http2StrAppend(&output, f);
  EXPECT_EQ("10", output);
  Http2StrAppend(&output, f, t, t, f);
  EXPECT_EQ("100110", output);

  output.clear();

  // Mixed string-like, numerical, and Boolean arguments.
  Http2StrAppend(&output, kFoo, i, string_foo, f, u, t, stringpiece_bar, d, t);
  EXPECT_EQ("foo1foo081bar3.14151", output);
  Http2StrAppend(&output, d, t, t, string_bar, i, u, kBar, t, d, f);
  EXPECT_EQ("foo1foo081bar3.141513.141511bar18bar13.14150", output);
}

TEST(Http2StringUtilsTest, Http2StringPrintf) {
  EXPECT_EQ("", Http2StringPrintf("%s", ""));
  EXPECT_EQ("foobar", Http2StringPrintf("%sbar", "foo"));
  EXPECT_EQ("foobar", Http2StringPrintf("%s%s", "foo", "bar"));
  EXPECT_EQ("foo: 1, bar: 2.0",
            Http2StringPrintf("foo: %d, bar: %.1f", 1, 2.0));
}

}  // namespace
}  // namespace test
}  // namespace http2
