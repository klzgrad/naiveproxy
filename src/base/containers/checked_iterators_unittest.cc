// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/containers/checked_iterators.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace base {

// Checks that constexpr CheckedContiguousConstIterators can be compared at
// compile time.
TEST(CheckedContiguousIterator, StaticComparisonOperators) {
  static constexpr int arr[] = {0};

  constexpr CheckedContiguousConstIterator<int> begin(arr, arr, arr + 1);
  constexpr CheckedContiguousConstIterator<int> end(arr, arr + 1, arr + 1);

  static_assert(begin == begin, "");
  static_assert(end == end, "");

  static_assert(begin != end, "");
  static_assert(end != begin, "");

  static_assert(begin < end, "");

  static_assert(begin <= begin, "");
  static_assert(begin <= end, "");
  static_assert(end <= end, "");

  static_assert(end > begin, "");

  static_assert(end >= end, "");
  static_assert(end >= begin, "");
  static_assert(begin >= begin, "");
}

// Checks that comparison between iterators and const iterators works in both
// directions.
TEST(CheckedContiguousIterator, ConvertingComparisonOperators) {
  static int arr[] = {0};

  CheckedContiguousIterator<int> begin(arr, arr, arr + 1);
  CheckedContiguousConstIterator<int> cbegin(arr, arr, arr + 1);

  CheckedContiguousIterator<int> end(arr, arr + 1, arr + 1);
  CheckedContiguousConstIterator<int> cend(arr, arr + 1, arr + 1);

  EXPECT_EQ(begin, cbegin);
  EXPECT_EQ(cbegin, begin);
  EXPECT_EQ(end, cend);
  EXPECT_EQ(cend, end);

  EXPECT_NE(begin, cend);
  EXPECT_NE(cbegin, end);
  EXPECT_NE(end, cbegin);
  EXPECT_NE(cend, begin);

  EXPECT_LT(begin, cend);
  EXPECT_LT(cbegin, end);

  EXPECT_LE(begin, cbegin);
  EXPECT_LE(cbegin, begin);
  EXPECT_LE(begin, cend);
  EXPECT_LE(cbegin, end);
  EXPECT_LE(end, cend);
  EXPECT_LE(cend, end);

  EXPECT_GT(end, cbegin);
  EXPECT_GT(cend, begin);

  EXPECT_GE(end, cend);
  EXPECT_GE(cend, end);
  EXPECT_GE(end, cbegin);
  EXPECT_GE(cend, begin);
  EXPECT_GE(begin, cbegin);
  EXPECT_GE(cbegin, begin);
}

}  // namespace base
