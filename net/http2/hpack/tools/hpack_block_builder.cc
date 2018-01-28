// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http2/hpack/tools/hpack_block_builder.h"

#include "net/http2/tools/http2_bug_tracker.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

void HpackBlockBuilder::AppendHighBitsAndVarint(uint8_t high_bits,
                                                uint8_t prefix_length,
                                                uint64_t varint) {
  EXPECT_LE(4, prefix_length);
  EXPECT_LE(prefix_length, 7);

  // prefix_mask defines the sequence of low-order bits of the first byte
  // that encode the prefix of the value. It is also the marker in those bits
  // of the first byte indicating that at least one extension byte is needed.
  uint8_t prefix_mask = (1 << prefix_length) - 1;
  EXPECT_EQ(0, high_bits & prefix_mask);

  if (varint < prefix_mask) {
    uint8_t b = high_bits | static_cast<uint8_t>(varint);
    buffer_.push_back(static_cast<char>(b));
    return;
  }

  // We need extension bytes.
  buffer_.push_back(static_cast<char>(high_bits | prefix_mask));
  varint -= prefix_mask;
  while (varint >= 128) {
    uint8_t b = static_cast<uint8_t>((varint % 128) + 128);
    buffer_.push_back(static_cast<char>(b));
    varint = varint / 128;
  }
  char c = static_cast<char>(varint);
  buffer_.push_back(c);
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
  }
  AppendHighBitsAndVarint(high_bits, prefix_length, varint);
}

void HpackBlockBuilder::AppendString(bool is_huffman_encoded,
                                     Http2StringPiece str) {
  uint8_t high_bits = is_huffman_encoded ? 0x80 : 0;
  uint8_t prefix_length = 7;
  AppendHighBitsAndVarint(high_bits, prefix_length, str.size());
  buffer_.append(str.data(), str.size());
}

}  // namespace test
}  // namespace net
