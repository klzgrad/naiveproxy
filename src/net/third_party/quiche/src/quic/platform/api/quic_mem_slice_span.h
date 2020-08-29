// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_SPAN_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_SPAN_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/quic/platform/impl/quic_mem_slice_span_impl.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

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

  // Constructs a span with a single QuicMemSlice.
  explicit QuicMemSliceSpan(QuicMemSlice* slice) : impl_(slice->impl()) {}

  QuicMemSliceSpan(const QuicMemSliceSpan& other) = default;
  QuicMemSliceSpan& operator=(const QuicMemSliceSpan& other) = default;
  QuicMemSliceSpan(QuicMemSliceSpan&& other) = default;
  QuicMemSliceSpan& operator=(QuicMemSliceSpan&& other) = default;

  ~QuicMemSliceSpan() = default;

  template <typename ConsumeFunction>
  QuicByteCount ConsumeAll(ConsumeFunction consume) {
    return impl_.ConsumeAll(consume);
  }

  // Return data of the span at |index| by the form of a
  // quiche::QuicheStringPiece.
  quiche::QuicheStringPiece GetData(int index) { return impl_.GetData(index); }

  // Return the total length of the data inside the span.
  QuicByteCount total_length() { return impl_.total_length(); }

  // Return total number of slices in the span.
  size_t NumSlices() { return impl_.NumSlices(); }

  bool empty() const { return impl_.empty(); }

 private:
  QuicMemSliceSpanImpl impl_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_SPAN_H_
