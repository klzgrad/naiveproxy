// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/http2/decoder/decode_buffer.h"

namespace http2 {

uint8_t DecodeBuffer::DecodeUInt8() {
  return static_cast<uint8_t>(DecodeChar());
}

uint16_t DecodeBuffer::DecodeUInt16() {
  DCHECK_LE(2u, Remaining());
  const uint8_t b1 = DecodeUInt8();
  const uint8_t b2 = DecodeUInt8();
  // Note that chars are automatically promoted to ints during arithmetic,
  // so the b1 << 8 doesn't end up as zero before being or-ed with b2.
  // And the left-shift operator has higher precedence than the or operator.
  return b1 << 8 | b2;
}

uint32_t DecodeBuffer::DecodeUInt24() {
  DCHECK_LE(3u, Remaining());
  const uint8_t b1 = DecodeUInt8();
  const uint8_t b2 = DecodeUInt8();
  const uint8_t b3 = DecodeUInt8();
  return b1 << 16 | b2 << 8 | b3;
}

uint32_t DecodeBuffer::DecodeUInt31() {
  DCHECK_LE(4u, Remaining());
  const uint8_t b1 = DecodeUInt8() & 0x7f;  // Mask out the high order bit.
  const uint8_t b2 = DecodeUInt8();
  const uint8_t b3 = DecodeUInt8();
  const uint8_t b4 = DecodeUInt8();
  return b1 << 24 | b2 << 16 | b3 << 8 | b4;
}

uint32_t DecodeBuffer::DecodeUInt32() {
  DCHECK_LE(4u, Remaining());
  const uint8_t b1 = DecodeUInt8();
  const uint8_t b2 = DecodeUInt8();
  const uint8_t b3 = DecodeUInt8();
  const uint8_t b4 = DecodeUInt8();
  return b1 << 24 | b2 << 16 | b3 << 8 | b4;
}

#ifndef NDEBUG
void DecodeBuffer::set_subset_of_base(DecodeBuffer* base,
                                      const DecodeBufferSubset* subset) {
  DCHECK_EQ(this, subset);
  base->set_subset(subset);
}
void DecodeBuffer::clear_subset_of_base(DecodeBuffer* base,
                                        const DecodeBufferSubset* subset) {
  DCHECK_EQ(this, subset);
  base->clear_subset(subset);
}
void DecodeBuffer::set_subset(const DecodeBufferSubset* subset) {
  DCHECK(subset != nullptr);
  DCHECK_EQ(subset_, nullptr) << "There is already a subset";
  subset_ = subset;
}
void DecodeBuffer::clear_subset(const DecodeBufferSubset* subset) {
  DCHECK(subset != nullptr);
  DCHECK_EQ(subset_, subset);
  subset_ = nullptr;
}
void DecodeBufferSubset::DebugSetup() {
  start_base_offset_ = base_buffer_->Offset();
  max_base_offset_ = start_base_offset_ + FullSize();
  DCHECK_LE(max_base_offset_, base_buffer_->FullSize());

  // Ensure that there is only one DecodeBufferSubset at a time for a base.
  set_subset_of_base(base_buffer_, this);
}
void DecodeBufferSubset::DebugTearDown() {
  // Ensure that the base hasn't been modified.
  DCHECK_EQ(start_base_offset_, base_buffer_->Offset())
      << "The base buffer was modified";

  // Ensure that we haven't gone beyond the maximum allowed offset.
  size_t offset = Offset();
  DCHECK_LE(offset, FullSize());
  DCHECK_LE(start_base_offset_ + offset, max_base_offset_);
  DCHECK_LE(max_base_offset_, base_buffer_->FullSize());

  clear_subset_of_base(base_buffer_, this);
}
#endif

}  // namespace http2
