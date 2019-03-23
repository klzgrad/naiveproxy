// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/quic_mem_slice_span_impl.h"

#include "net/third_party/quic/core/quic_stream_send_buffer.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"

namespace quic {

QuicMemSliceSpanImpl::QuicMemSliceSpanImpl(
    const scoped_refptr<net::IOBuffer>* buffers,
    const int* lengths,
    size_t num_buffers)
    : buffers_(buffers), lengths_(lengths), num_buffers_(num_buffers) {}

QuicMemSliceSpanImpl::QuicMemSliceSpanImpl(const QuicMemSliceSpanImpl& other) =
    default;
QuicMemSliceSpanImpl& QuicMemSliceSpanImpl::operator=(
    const QuicMemSliceSpanImpl& other) = default;
QuicMemSliceSpanImpl::QuicMemSliceSpanImpl(QuicMemSliceSpanImpl&& other) =
    default;
QuicMemSliceSpanImpl& QuicMemSliceSpanImpl::operator=(
    QuicMemSliceSpanImpl&& other) = default;

QuicMemSliceSpanImpl::~QuicMemSliceSpanImpl() = default;

QuicByteCount QuicMemSliceSpanImpl::SaveMemSlicesInSendBuffer(
    QuicStreamSendBuffer* send_buffer) {
  size_t saved_length = 0;
  for (size_t i = 0; i < num_buffers_; ++i) {
    if (lengths_[i] == 0) {
      // Skip empty buffer.
      continue;
    }
    saved_length += lengths_[i];
    send_buffer->SaveMemSlice(
        QuicMemSlice(QuicMemSliceImpl(buffers_[i], lengths_[i])));
  }
  return saved_length;
}

QuicByteCount QuicMemSliceSpanImpl::total_length() {
  QuicByteCount length = 0;
  for (size_t i = 0; i < num_buffers_; ++i) {
    length += lengths_[i];
  }
  return length;
}

}  // namespace quic
