// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/qpack/qpack_required_insert_count.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {
namespace test {
namespace {

TEST(QpackRequiredInsertCountTest, QpackEncodeRequiredInsertCount) {
  EXPECT_EQ(0u, QpackEncodeRequiredInsertCount(0, 0));
  EXPECT_EQ(0u, QpackEncodeRequiredInsertCount(0, 8));
  EXPECT_EQ(0u, QpackEncodeRequiredInsertCount(0, 1024));

  EXPECT_EQ(2u, QpackEncodeRequiredInsertCount(1, 8));
  EXPECT_EQ(5u, QpackEncodeRequiredInsertCount(20, 8));
  EXPECT_EQ(7u, QpackEncodeRequiredInsertCount(106, 10));
}

// For testing valid decodings, the Encoded Required Insert Count is calculated
// from Required Insert Count, so that there is an expected value to compare
// the decoded value against, and so that intricate inequalities can be
// documented.
struct {
  uint64_t required_insert_count;
  uint64_t max_entries;
  uint64_t total_number_of_inserts;
} kTestData[] = {
    // Maximum dynamic table capacity is zero.
    {0, 0, 0},
    // No dynamic entries in header.
    {0, 100, 0},
    {0, 100, 500},
    // Required Insert Count has not wrapped around yet, no entries evicted.
    {15, 100, 25},
    {20, 100, 10},
    // Required Insert Count has not wrapped around yet, some entries evicted.
    {90, 100, 110},
    // Required Insert Count has wrapped around.
    {234, 100, 180},
    // Required Insert Count has wrapped around many times.
    {5678, 100, 5701},
    // Lowest and highest possible Required Insert Count values
    // for given MaxEntries and total number of insertions.
    {401, 100, 500},
    {600, 100, 500}};

TEST(QpackRequiredInsertCountTest, QpackDecodeRequiredInsertCount) {
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kTestData); ++i) {
    const uint64_t required_insert_count = kTestData[i].required_insert_count;
    const uint64_t max_entries = kTestData[i].max_entries;
    const uint64_t total_number_of_inserts =
        kTestData[i].total_number_of_inserts;

    if (required_insert_count != 0) {
      // Dynamic entries cannot be referenced if dynamic table capacity is zero.
      ASSERT_LT(0u, max_entries) << i;
      // Entry |total_number_of_inserts - 1 - max_entries| and earlier entries
      // are evicted.  Entry |required_insert_count - 1| is referenced.  No
      // evicted entry can be referenced.
      ASSERT_LT(total_number_of_inserts, required_insert_count + max_entries)
          << i;
      // Entry |required_insert_count - 1 - max_entries| and earlier entries are
      // evicted, entry |total_number_of_inserts - 1| is the last acknowledged
      // entry.  Every evicted entry must be acknowledged.
      ASSERT_LE(required_insert_count, total_number_of_inserts + max_entries)
          << i;
    }

    uint64_t encoded_required_insert_count =
        QpackEncodeRequiredInsertCount(required_insert_count, max_entries);

    // Initialize to a value different from the expected output to confirm that
    // QpackDecodeRequiredInsertCount() modifies the value of
    // |decoded_required_insert_count|.
    uint64_t decoded_required_insert_count = required_insert_count + 1;
    EXPECT_TRUE(QpackDecodeRequiredInsertCount(
        encoded_required_insert_count, max_entries, total_number_of_inserts,
        &decoded_required_insert_count))
        << i;

    EXPECT_EQ(decoded_required_insert_count, required_insert_count) << i;
  }
}

// Failures are tested with hardcoded values for encoded required insert count,
// to provide test coverage for values that would never be produced by a well
// behaved encoding function.
struct {
  uint64_t encoded_required_insert_count;
  uint64_t max_entries;
  uint64_t total_number_of_inserts;
} kInvalidTestData[] = {
    // Maximum dynamic table capacity is zero, yet header block
    // claims to have a reference to a dynamic table entry.
    {1, 0, 0},
    {9, 0, 0},
    // Examples from
    // https://github.com/quicwg/base-drafts/issues/2112#issue-389626872.
    {1, 10, 2},
    {18, 10, 2},
    // Encoded Required Insert Count value too small or too large
    // for given MaxEntries and total number of insertions.
    {400, 100, 500},
    {601, 100, 500}};

TEST(QpackRequiredInsertCountTest, DecodeRequiredInsertCountError) {
  for (size_t i = 0; i < QUICHE_ARRAYSIZE(kInvalidTestData); ++i) {
    uint64_t decoded_required_insert_count = 0;
    EXPECT_FALSE(QpackDecodeRequiredInsertCount(
        kInvalidTestData[i].encoded_required_insert_count,
        kInvalidTestData[i].max_entries,
        kInvalidTestData[i].total_number_of_inserts,
        &decoded_required_insert_count))
        << i;
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
