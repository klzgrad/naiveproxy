// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_MEM_SLICE_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_MEM_SLICE_IMPL_H_

#include "base/memory/ref_counted.h"
#include "net/base/io_buffer.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

class QuicBufferAllocator;

// QuicMemSliceImpl TODO(fayang)
class QUIC_EXPORT_PRIVATE QuicMemSliceImpl {
 public:
  // Constructs an empty QuicMemSliceImpl.
  QuicMemSliceImpl();
  // Constructs a QuicMemSliceImp by let |allocator| allocate a data buffer of
  // |length|.
  QuicMemSliceImpl(QuicBufferAllocator* allocator, size_t length);

  QuicMemSliceImpl(scoped_refptr<IOBuffer> io_buffer, size_t length);

  QuicMemSliceImpl(const QuicMemSliceImpl& other) = delete;
  QuicMemSliceImpl& operator=(const QuicMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  QuicMemSliceImpl(QuicMemSliceImpl&& other);
  QuicMemSliceImpl& operator=(QuicMemSliceImpl&& other);

  ~QuicMemSliceImpl();

  // Returns a char pointer to underlying data buffer.
  const char* data() const;
  // Returns the length of underlying data buffer.
  size_t length() const { return length_; }

  bool empty() const { return length_ == 0; }

 private:
  scoped_refptr<IOBuffer> io_buffer_;
  // Length of io_buffer_.
  size_t length_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_MEM_SLICE_IMPL_H_
