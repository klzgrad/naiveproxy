// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/decoder/hpack_entry_type_decoder.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_logging.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_test_helpers.h"
#include "net/third_party/quiche/src/http2/tools/random_decoder_test.h"

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

namespace http2 {
namespace test {
namespace {
const bool kReturnNonZeroOnFirst = true;

class HpackEntryTypeDecoderTest : public RandomDecoderTest {
 protected:
  DecodeStatus StartDecoding(DecodeBuffer* b) override {
    CHECK_LT(0u, b->Remaining());
    return decoder_.Start(b);
  }

  DecodeStatus ResumeDecoding(DecodeBuffer* b) override {
    return decoder_.Resume(b);
  }

  HpackEntryTypeDecoder decoder_;
};

TEST_F(HpackEntryTypeDecoderTest, DynamicTableSizeUpdate) {
  for (uint32_t size = 0; size < 1000 * 1000; size += 256) {
    HpackBlockBuilder bb;
    bb.AppendDynamicTableSizeUpdate(size);
    DecodeBuffer db(bb.buffer());
    auto validator = [size, this]() -> AssertionResult {
      VERIFY_EQ(HpackEntryType::kDynamicTableSizeUpdate, decoder_.entry_type());
      VERIFY_EQ(size, decoder_.varint());
      return AssertionSuccess();
    };
    EXPECT_TRUE(DecodeAndValidateSeveralWays(&db, kReturnNonZeroOnFirst,
                                             ValidateDoneAndEmpty(validator)))
        << "\nentry_type=kDynamicTableSizeUpdate, size=" << size;
    // Run the validator again to make sure that DecodeAndValidateSeveralWays
    // did the right thing.
    EXPECT_TRUE(validator());
  }
}

TEST_F(HpackEntryTypeDecoderTest, HeaderWithIndex) {
  std::vector<HpackEntryType> entry_types = {
      HpackEntryType::kIndexedHeader,
      HpackEntryType::kIndexedLiteralHeader,
      HpackEntryType::kUnindexedLiteralHeader,
      HpackEntryType::kNeverIndexedLiteralHeader,
  };
  for (const HpackEntryType entry_type : entry_types) {
    const uint32_t first = entry_type == HpackEntryType::kIndexedHeader ? 1 : 0;
    for (uint32_t index = first; index < 1000; ++index) {
      HpackBlockBuilder bb;
      bb.AppendEntryTypeAndVarint(entry_type, index);
      DecodeBuffer db(bb.buffer());
      auto validator = [entry_type, index, this]() -> AssertionResult {
        VERIFY_EQ(entry_type, decoder_.entry_type());
        VERIFY_EQ(index, decoder_.varint());
        return AssertionSuccess();
      };
      EXPECT_TRUE(DecodeAndValidateSeveralWays(&db, kReturnNonZeroOnFirst,
                                               ValidateDoneAndEmpty(validator)))
          << "\nentry_type=" << entry_type << ", index=" << index;
      // Run the validator again to make sure that DecodeAndValidateSeveralWays
      // did the right thing.
      EXPECT_TRUE(validator());
    }
  }
}

}  // namespace
}  // namespace test
}  // namespace http2
