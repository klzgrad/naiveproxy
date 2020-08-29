// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/util/ranges/functional.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace util {

TEST(RangesTest, Identity) {
  static constexpr identity id;

  std::vector<int> v;
  EXPECT_EQ(&v, &id(v));

  constexpr int arr = {0};
  static_assert(arr == id(arr), "");
}

TEST(RangesTest, Invoke) {
  struct S {
    int data_member = 123;

    int member_function() { return 42; }
  };

  S s;
  EXPECT_EQ(123, invoke(&S::data_member, s));
  EXPECT_EQ(42, invoke(&S::member_function, s));

  auto add_functor = [](int i, int j) { return i + j; };
  EXPECT_EQ(3, invoke(add_functor, 1, 2));
}

TEST(RangesTest, EqualTo) {
  ranges::equal_to eq;
  EXPECT_TRUE(eq(0, 0));
  EXPECT_FALSE(eq(0, 1));
  EXPECT_FALSE(eq(1, 0));
}

TEST(RangesTest, Less) {
  ranges::less lt;
  EXPECT_FALSE(lt(0, 0));
  EXPECT_TRUE(lt(0, 1));
  EXPECT_FALSE(lt(1, 0));
}

}  // namespace util
