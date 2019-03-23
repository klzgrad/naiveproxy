// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/qpack/qpack_progressive_decoder.h"

#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

// For testing valid decodings, the encoded (wire) largest reference value is
// calculated for actual Largest Reference values, so that there is an expected
// value to comparte the decoded value against, and so that intricate
// inequalities can be documented.
struct {
  uint64_t largest_reference;
  uint64_t max_entries;
  uint64_t total_number_of_inserts;
} kTestData[] = {
    // Maximum dynamic table capacity is zero.
    {0, 0, 0},
    // No dynamic entries in header.
    {0, 100, 0},
    {0, 100, 500},
    // Largest Reference has not wrapped around yet, no entries evicted.
    {15, 100, 25},
    {20, 100, 10},
    // Largest Reference has not wrapped around yet, some entries evicted.
    {90, 100, 110},
    // Largest Reference has wrapped around.
    {234, 100, 180},
    // Largest Reference has wrapped around many times.
    {5678, 100, 5701},
    // Lowest and highest possible Largest Reference values
    // for given MaxEntries and total number of insertions.
    {401, 100, 500},
    {600, 100, 500}};

uint64_t EncodeLargestReference(uint64_t largest_reference,
                                uint64_t max_entries) {
  if (largest_reference == 0) {
    return 0;
  }

  return largest_reference % (2 * max_entries) + 1;
}

TEST(QpackProgressiveDecoderTest, DecodeLargestReference) {
  for (size_t i = 0; i < QUIC_ARRAYSIZE(kTestData); ++i) {
    const uint64_t largest_reference = kTestData[i].largest_reference;
    const uint64_t max_entries = kTestData[i].max_entries;
    const uint64_t total_number_of_inserts =
        kTestData[i].total_number_of_inserts;

    if (largest_reference != 0) {
      // Dynamic entries cannot be referenced if dynamic table capacity is zero.
      ASSERT_LT(0u, max_entries) << i;
      // Entry |total_number_of_inserts - max_entries| and earlier entries are
      // evicted.  Entry |largest_reference| is referenced.  No evicted entry
      // can be referenced.
      ASSERT_LT(total_number_of_inserts, largest_reference + max_entries) << i;
      // Entry |largest_reference - max_entries| and earlier entries are
      // evicted, entry |total_number_of_inserts| is the last acknowledged
      // entry.  Every evicted entry must be acknowledged.
      ASSERT_LE(largest_reference, total_number_of_inserts + max_entries) << i;
    }

    uint64_t wire_largest_reference =
        EncodeLargestReference(largest_reference, max_entries);

    // Initialize to a value different from the expected output to confirm that
    // DecodeLargestReference() modifies the value of
    // |decoded_largest_reference|.
    uint64_t decoded_largest_reference = largest_reference + 1;
    EXPECT_TRUE(QpackProgressiveDecoder::DecodeLargestReference(
        wire_largest_reference, max_entries, total_number_of_inserts,
        &decoded_largest_reference))
        << i;

    EXPECT_EQ(decoded_largest_reference, largest_reference) << i;
  }
}

// Failures are tested with hardcoded values for the on-the-wire largest
// reference field, to provide test coverage for values that would never be
// produced by a well behaved encoding function.
struct {
  uint64_t wire_largest_reference;
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
    // Largest Reference value too small or too large
    // for given MaxEntries and total number of insertions.
    {400, 100, 500},
    {601, 100, 500}};

TEST(QpackProgressiveDecoderTest, DecodeLargestReferenceError) {
  for (size_t i = 0; i < QUIC_ARRAYSIZE(kInvalidTestData); ++i) {
    uint64_t decoded_largest_reference = 0;
    EXPECT_FALSE(QpackProgressiveDecoder::DecodeLargestReference(
        kInvalidTestData[i].wire_largest_reference,
        kInvalidTestData[i].max_entries,
        kInvalidTestData[i].total_number_of_inserts,
        &decoded_largest_reference))
        << i;
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
