// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/hpack/tools/hpack_block_builder.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "net/third_party/quiche/src/http2/hpack/varint/hpack_varint_encoder.h"
#include "net/third_party/quiche/src/http2/platform/api/http2_bug_tracker.h"

namespace http2 {
namespace test {

void HpackBlockBuilder::AppendHighBitsAndVarint(uint8_t high_bits,
                                                uint8_t prefix_length,
                                                uint64_t varint) {
  EXPECT_LE(3, prefix_length);
  EXPECT_LE(prefix_length, 8);

  HpackVarintEncoder::Encode(high_bits, prefix_length, varint, &buffer_);
}

void HpackBlockBuilder::AppendEntryTypeAndVarint(HpackEntryType entry_type,
                                                 uint64_t varint) {
  uint8_t high_bits;
  uint8_t prefix_length;  // Bits of the varint prefix in the first byte.
  switch (entry_type) {
    case HpackEntryType::kIndexedHeader:
      high_bits = 0x80;
      prefix_length = 7;
      break;
    case HpackEntryType::kDynamicTableSizeUpdate:
      high_bits = 0x20;
      prefix_length = 5;
      break;
    case HpackEntryType::kIndexedLiteralHeader:
      high_bits = 0x40;
      prefix_length = 6;
      break;
    case HpackEntryType::kUnindexedLiteralHeader:
      high_bits = 0x00;
      prefix_length = 4;
      break;
    case HpackEntryType::kNeverIndexedLiteralHeader:
      high_bits = 0x10;
      prefix_length = 4;
      break;
    default:
      HTTP2_BUG << "Unreached, entry_type=" << entry_type;
      high_bits = 0;
      prefix_length = 0;
      break;
  }
  AppendHighBitsAndVarint(high_bits, prefix_length, varint);
}

void HpackBlockBuilder::AppendString(bool is_huffman_encoded,
                                     quiche::QuicheStringPiece str) {
  uint8_t high_bits = is_huffman_encoded ? 0x80 : 0;
  uint8_t prefix_length = 7;
  AppendHighBitsAndVarint(high_bits, prefix_length, str.size());
  buffer_.append(str.data(), str.size());
}

}  // namespace test
}  // namespace http2
