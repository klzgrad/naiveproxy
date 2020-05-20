// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_string_utils.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

TEST(QuicStringUtilsTest, QuicheStrCat) {
  // No arguments.
  EXPECT_EQ("", quiche::QuicheStrCat());

  // Single string-like argument.
  const char kFoo[] = "foo";
  const std::string string_foo(kFoo);
  const quiche::QuicheStringPiece stringpiece_foo(string_foo);
  EXPECT_EQ("foo", quiche::QuicheStrCat(kFoo));
  EXPECT_EQ("foo", quiche::QuicheStrCat(string_foo));
  EXPECT_EQ("foo", quiche::QuicheStrCat(stringpiece_foo));

  // Two string-like arguments.
  const char kBar[] = "bar";
  const quiche::QuicheStringPiece stringpiece_bar(kBar);
  const std::string string_bar(kBar);
  EXPECT_EQ("foobar", quiche::QuicheStrCat(kFoo, kBar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(kFoo, string_bar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(kFoo, stringpiece_bar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(string_foo, kBar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(string_foo, string_bar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(string_foo, stringpiece_bar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(stringpiece_foo, kBar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(stringpiece_foo, string_bar));
  EXPECT_EQ("foobar", quiche::QuicheStrCat(stringpiece_foo, stringpiece_bar));

  // Many-many arguments.
  EXPECT_EQ("foobarbazquxquuxquuzcorgegraultgarplywaldofredplughxyzzythud",
            quiche::QuicheStrCat("foo", "bar", "baz", "qux", "quux", "quuz",
                                 "corge", "grault", "garply", "waldo", "fred",
                                 "plugh", "xyzzy", "thud"));

  // Numerical arguments.
  const int16_t i = 1;
  const uint64_t u = 8;
  const double d = 3.1415;

  EXPECT_EQ("1 8", quiche::QuicheStrCat(i, " ", u));
  EXPECT_EQ("3.14151181", quiche::QuicheStrCat(d, i, i, u, i));
  EXPECT_EQ("i: 1, u: 8, d: 3.1415",
            quiche::QuicheStrCat("i: ", i, ", u: ", u, ", d: ", d));

  // Boolean arguments.
  const bool t = true;
  const bool f = false;

  EXPECT_EQ("1", quiche::QuicheStrCat(t));
  EXPECT_EQ("0", quiche::QuicheStrCat(f));
  EXPECT_EQ("0110", quiche::QuicheStrCat(f, t, t, f));

  // Mixed string-like, numerical, and Boolean arguments.
  EXPECT_EQ("foo1foo081bar3.14151",
            quiche::QuicheStrCat(kFoo, i, string_foo, f, u, t, stringpiece_bar,
                                 d, t));
  EXPECT_EQ("3.141511bar18bar13.14150",
            quiche::QuicheStrCat(d, t, t, string_bar, i, u, kBar, t, d, f));
}

TEST(QuicStringUtilsTest, QuicStrAppend) {
  // No arguments on empty string.
  std::string output;
  QuicStrAppend(&output);
  EXPECT_TRUE(output.empty());

  // Single string-like argument.
  const char kFoo[] = "foo";
  const std::string string_foo(kFoo);
  const quiche::QuicheStringPiece stringpiece_foo(string_foo);
  QuicStrAppend(&output, kFoo);
  EXPECT_EQ("foo", output);
  QuicStrAppend(&output, string_foo);
  EXPECT_EQ("foofoo", output);
  QuicStrAppend(&output, stringpiece_foo);
  EXPECT_EQ("foofoofoo", output);

  // No arguments on non-empty string.
  QuicStrAppend(&output);
  EXPECT_EQ("foofoofoo", output);

  output.clear();

  // Two string-like arguments.
  const char kBar[] = "bar";
  const quiche::QuicheStringPiece stringpiece_bar(kBar);
  const std::string string_bar(kBar);
  QuicStrAppend(&output, kFoo, kBar);
  EXPECT_EQ("foobar", output);
  QuicStrAppend(&output, kFoo, string_bar);
  EXPECT_EQ("foobarfoobar", output);
  QuicStrAppend(&output, kFoo, stringpiece_bar);
  EXPECT_EQ("foobarfoobarfoobar", output);
  QuicStrAppend(&output, string_foo, kBar);
  EXPECT_EQ("foobarfoobarfoobarfoobar", output);

  output.clear();

  QuicStrAppend(&output, string_foo, string_bar);
  EXPECT_EQ("foobar", output);
  QuicStrAppend(&output, string_foo, stringpiece_bar);
  EXPECT_EQ("foobarfoobar", output);
  QuicStrAppend(&output, stringpiece_foo, kBar);
  EXPECT_EQ("foobarfoobarfoobar", output);
  QuicStrAppend(&output, stringpiece_foo, string_bar);
  EXPECT_EQ("foobarfoobarfoobarfoobar", output);

  output.clear();

  QuicStrAppend(&output, stringpiece_foo, stringpiece_bar);
  EXPECT_EQ("foobar", output);

  // Many-many arguments.
  QuicStrAppend(&output, "foo", "bar", "baz", "qux", "quux", "quuz", "corge",
                "grault", "garply", "waldo", "fred", "plugh", "xyzzy", "thud");
  EXPECT_EQ(
      "foobarfoobarbazquxquuxquuzcorgegraultgarplywaldofredplughxyzzythud",
      output);

  output.clear();

  // Numerical arguments.
  const int16_t i = 1;
  const uint64_t u = 8;
  const double d = 3.1415;

  QuicStrAppend(&output, i, " ", u);
  EXPECT_EQ("1 8", output);
  QuicStrAppend(&output, d, i, i, u, i);
  EXPECT_EQ("1 83.14151181", output);
  QuicStrAppend(&output, "i: ", i, ", u: ", u, ", d: ", d);
  EXPECT_EQ("1 83.14151181i: 1, u: 8, d: 3.1415", output);

  output.clear();

  // Boolean arguments.
  const bool t = true;
  const bool f = false;

  QuicStrAppend(&output, t);
  EXPECT_EQ("1", output);
  QuicStrAppend(&output, f);
  EXPECT_EQ("10", output);
  QuicStrAppend(&output, f, t, t, f);
  EXPECT_EQ("100110", output);

  output.clear();

  // Mixed string-like, numerical, and Boolean arguments.
  QuicStrAppend(&output, kFoo, i, string_foo, f, u, t, stringpiece_bar, d, t);
  EXPECT_EQ("foo1foo081bar3.14151", output);
  QuicStrAppend(&output, d, t, t, string_bar, i, u, kBar, t, d, f);
  EXPECT_EQ("foo1foo081bar3.141513.141511bar18bar13.14150", output);
}

TEST(QuicStringUtilsTest, QuicStringPrintf) {
  EXPECT_EQ("", quiche::QuicheStringPrintf("%s", ""));
  EXPECT_EQ("foobar", quiche::QuicheStringPrintf("%sbar", "foo"));
  EXPECT_EQ("foobar", quiche::QuicheStringPrintf("%s%s", "foo", "bar"));
  EXPECT_EQ("foo: 1, bar: 2.0",
            quiche::QuicheStringPrintf("foo: %d, bar: %.1f", 1, 2.0));
}

}  // namespace
}  // namespace test
}  // namespace quic
