// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_MEM_SLICE_H_
#define NET_QUIC_PLATFORM_API_QUIC_MEM_SLICE_H_

#include "net/quic/platform/impl/quic_mem_slice_impl.h"

namespace net {

// QuicMemSlice is an internally reference counted data buffer used as the
// source buffers for write operations. QuicMemSlice implicitly maintains a
// reference count and will free the underlying data buffer when the reference
// count reaches zero.
class QUIC_EXPORT_PRIVATE QuicMemSlice {
 public:
  // Constructs a empty QuicMemSlice with no underlying data and 0 reference
  // count.
  QuicMemSlice() = default;
  // Let |allocator| allocate a data buffer of |length|, then construct
  // QuicMemSlice with reference count 1 from the allocated data buffer.
  // Once all of the references to the allocated data buffer are released,
  // |allocator| is responsible to free the memory. |allocator| must
  // not be null, and |length| must not be 0. To construct an empty
  // QuicMemSlice, use the zero-argument constructor instead.
  QuicMemSlice(QuicBufferAllocator* allocator, size_t length)
      : impl_(allocator, length) {}

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

  // Returns a const char pointer to underlying data buffer.
  const char* data() const { return impl_.data(); }
  // Returns the length of underlying data buffer.
  size_t length() const { return impl_.length(); }

  bool empty() const { return impl_.empty(); }

 private:
  QuicMemSliceImpl impl_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_MEM_SLICE_H_
