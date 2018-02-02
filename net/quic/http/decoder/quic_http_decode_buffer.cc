// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/http/decoder/quic_http_decode_buffer.h"

namespace net {

#ifndef NDEBUG
// These are part of validating during tests that there is at most one
// QuicHttpDecodeBufferSubset instance at a time for any DecodeBuffer instance.
void QuicHttpDecodeBuffer::set_subset_of_base(
    QuicHttpDecodeBuffer* base,
    const QuicHttpDecodeBufferSubset* subset) {
  DCHECK_EQ(this, subset);
  base->set_subset(subset);
}
void QuicHttpDecodeBuffer::clear_subset_of_base(
    QuicHttpDecodeBuffer* base,
    const QuicHttpDecodeBufferSubset* subset) {
  DCHECK_EQ(this, subset);
  base->clear_subset(subset);
}
void QuicHttpDecodeBuffer::set_subset(
    const QuicHttpDecodeBufferSubset* subset) {
  DCHECK(subset != nullptr);
  DCHECK_EQ(subset_, nullptr) << "There is already a subset";
  subset_ = subset;
}
void QuicHttpDecodeBuffer::clear_subset(
    const QuicHttpDecodeBufferSubset* subset) {
  DCHECK(subset != nullptr);
  DCHECK_EQ(subset_, subset);
  subset_ = nullptr;
}
void QuicHttpDecodeBufferSubset::DebugSetup() {
  start_base_offset_ = base_buffer_->Offset();
  max_base_offset_ = start_base_offset_ + FullSize();
  DCHECK_LE(max_base_offset_, base_buffer_->FullSize());

  // Ensure that there is only one QuicHttpDecodeBufferSubset at a time for a
  // base.
  set_subset_of_base(base_buffer_, this);
}
void QuicHttpDecodeBufferSubset::DebugTearDown() {
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

}  // namespace net
