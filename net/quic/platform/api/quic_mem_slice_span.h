// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_MEM_SLICE_SPAN_H_
#define NET_QUIC_PLATFORM_API_QUIC_MEM_SLICE_SPAN_H_

#include "net/quic/platform/impl/quic_mem_slice_span_impl.h"

namespace net {

// QuicMemSliceSpan is effectively wrapper around an array of data structures
// used as QuicMemSlice. So it could implemented with:
// QuicMemSlice* slices_;
// size_t num_slices_;
// But for efficiency reasons, the actual implementation is an array of
// platform-specific objects. This could avoid the translation from
// platform-specific object to QuicMemSlice.
// QuicMemSliceSpan does not own the underling data buffers.
class QUIC_EXPORT_PRIVATE QuicMemSliceSpan {
 public:
  explicit QuicMemSliceSpan(QuicMemSliceSpanImpl impl) : impl_(impl) {}

  QuicMemSliceSpan(const QuicMemSliceSpan& other) = default;
  QuicMemSliceSpan& operator=(const QuicMemSliceSpan& other) = default;
  QuicMemSliceSpan(QuicMemSliceSpan&& other) = default;
  QuicMemSliceSpan& operator=(QuicMemSliceSpan&& other) = default;

  ~QuicMemSliceSpan() = default;

  // Save data buffers to |send_buffer| and returns the amount of saved data.
  // |send_buffer| will hold a reference to all data buffer.
  QuicByteCount SaveMemSlicesInSendBuffer(QuicStreamSendBuffer* send_buffer) {
    return impl_.SaveMemSlicesInSendBuffer(send_buffer);
  }

  bool empty() const { return impl_.empty(); }

 private:
  QuicMemSliceSpanImpl impl_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_MEM_SLICE_SPAN_H_
