// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/util/ranges/iterator.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace util {

namespace {

struct S {
  std::vector<int> v;
};

auto begin(const S& s) {
  return s.v.begin();
}

auto end(const S& s) {
  return s.v.end();
}

}  // namespace

TEST(RangesTest, Begin) {
  std::vector<int> vec;
  int arr[1]{};
  S s;

  EXPECT_EQ(vec.begin(), ranges::begin(vec));
  EXPECT_EQ(arr, ranges::begin(arr));
  EXPECT_EQ(s.v.begin(), ranges::begin(s));
}

TEST(RangesTest, End) {
  std::vector<int> vec;
  int arr[1]{};
  S s;

  EXPECT_EQ(vec.end(), ranges::end(vec));
  EXPECT_EQ(arr + 1, ranges::end(arr));
  EXPECT_EQ(s.v.end(), ranges::end(s));
}

}  // namespace util
