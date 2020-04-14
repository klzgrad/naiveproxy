// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_index_conversions.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

struct {
  uint64_t relative_index;
  uint64_t inserted_entry_count;
  uint64_t expected_absolute_index;
} kEncoderStreamRelativeIndexTestData[] = {{0, 1, 0},  {0, 2, 1},  {1, 2, 0},
                                           {0, 10, 9}, {5, 10, 4}, {9, 10, 0}};

TEST(QpackIndexConversions, EncoderStreamRelativeIndex) {
  for (const auto& test_data : kEncoderStreamRelativeIndexTestData) {
    uint64_t absolute_index = 42;
    EXPECT_TRUE(QpackEncoderStreamRelativeIndexToAbsoluteIndex(
        test_data.relative_index, test_data.inserted_entry_count,
        &absolute_index));
    EXPECT_EQ(test_data.expected_absolute_index, absolute_index);

    EXPECT_EQ(test_data.relative_index,
              QpackAbsoluteIndexToEncoderStreamRelativeIndex(
                  absolute_index, test_data.inserted_entry_count));
  }
}

struct {
  uint64_t relative_index;
  uint64_t base;
  uint64_t expected_absolute_index;
} kRequestStreamRelativeIndexTestData[] = {{0, 1, 0},  {0, 2, 1},  {1, 2, 0},
                                           {0, 10, 9}, {5, 10, 4}, {9, 10, 0}};

TEST(QpackIndexConversions, RequestStreamRelativeIndex) {
  for (const auto& test_data : kRequestStreamRelativeIndexTestData) {
    uint64_t absolute_index = 42;
    EXPECT_TRUE(QpackRequestStreamRelativeIndexToAbsoluteIndex(
        test_data.relative_index, test_data.base, &absolute_index));
    EXPECT_EQ(test_data.expected_absolute_index, absolute_index);

    EXPECT_EQ(test_data.relative_index,
              QpackAbsoluteIndexToRequestStreamRelativeIndex(absolute_index,
                                                             test_data.base));
  }
}

struct {
  uint64_t post_base_index;
  uint64_t base;
  uint64_t expected_absolute_index;
} kPostBaseIndexTestData[] = {{0, 1, 1}, {1, 0, 1}, {2, 0, 2},
                              {1, 1, 2}, {0, 2, 2}, {1, 2, 3}};

TEST(QpackIndexConversions, PostBaseIndex) {
  for (const auto& test_data : kPostBaseIndexTestData) {
    uint64_t absolute_index = 42;
    EXPECT_TRUE(QpackPostBaseIndexToAbsoluteIndex(
        test_data.post_base_index, test_data.base, &absolute_index));
    EXPECT_EQ(test_data.expected_absolute_index, absolute_index);
  }
}

TEST(QpackIndexConversions, EncoderStreamRelativeIndexUnderflow) {
  uint64_t absolute_index;
  EXPECT_FALSE(QpackEncoderStreamRelativeIndexToAbsoluteIndex(
      /* relative_index = */ 10,
      /* inserted_entry_count = */ 10, &absolute_index));
  EXPECT_FALSE(QpackEncoderStreamRelativeIndexToAbsoluteIndex(
      /* relative_index = */ 12,
      /* inserted_entry_count = */ 10, &absolute_index));
}

TEST(QpackIndexConversions, RequestStreamRelativeIndexUnderflow) {
  uint64_t absolute_index;
  EXPECT_FALSE(QpackRequestStreamRelativeIndexToAbsoluteIndex(
      /* relative_index = */ 10,
      /* base = */ 10, &absolute_index));
  EXPECT_FALSE(QpackRequestStreamRelativeIndexToAbsoluteIndex(
      /* relative_index = */ 12,
      /* base = */ 10, &absolute_index));
}

TEST(QpackIndexConversions, QpackPostBaseIndexToAbsoluteIndexOverflow) {
  uint64_t absolute_index;
  EXPECT_FALSE(QpackPostBaseIndexToAbsoluteIndex(
      /* post_base_index = */ 20,
      /* base = */ std::numeric_limits<uint64_t>::max() - 10, &absolute_index));
}

}  // namespace
}  // namespace test
}  // namespace quic
