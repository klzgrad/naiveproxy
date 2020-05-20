// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/quic/platform/impl/quic_mem_slice_impl.h"

/* API_DESCRIPTION
 QuicMemSlice is used to wrap application data and pass to QUIC stream's write
 interface. It refers to a memory block of data which should be around till
 QuicMemSlice::Reset() is called. It's upto each platform, to implement it as
 reference counted or not.
 API-DESCRIPTION */

namespace quic {

// QuicMemSlice is an internally reference counted data buffer used as the
// source buffers for write operations. QuicMemSlice implicitly maintains a
// reference count and will free the underlying data buffer when the reference
// count reaches zero.
class QUIC_EXPORT_PRIVATE QuicMemSlice {
 public:
  // Constructs a empty QuicMemSlice with no underlying data and 0 reference
  // count.
  QuicMemSlice() = default;

  // Constructs a QuicMemSlice that takes ownership of |buffer|.  |length| must
  // not be zero.  To construct an empty QuicMemSlice, use the zero-argument
  // constructor instead.
  QuicMemSlice(QuicUniqueBufferPtr buffer, size_t length)
      : impl_(std::move(buffer), length) {}

  // Constructs QuicMemSlice from |impl|. It takes the reference away from
  // |impl|.
  explicit QuicMemSlice(QuicMemSliceImpl impl) : impl_(std::move(impl)) {}

  QuicMemSlice(const QuicMemSlice& other) = delete;
  QuicMemSlice& operator=(const QuicMemSlice& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSlice(QuicMemSlice&& other) = default;
  QuicMemSlice& operator=(QuicMemSlice&& other) = default;

  ~QuicMemSlice() = default;

  // Release the underlying reference. Further access the memory will result in
  // undefined behavior.
  void Reset() { impl_.Reset(); }

  // Returns a const char pointer to underlying data buffer.
  const char* data() const { return impl_.data(); }
  // Returns the length of underlying data buffer.
  size_t length() const { return impl_.length(); }

  bool empty() const { return impl_.empty(); }

  QuicMemSliceImpl* impl() { return &impl_; }

 private:
  QuicMemSliceImpl impl_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_H_
