// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SPDY_PLATFORM_IMPL_SPDY_MEM_SLICE_IMPL_H_
#define NET_SPDY_PLATFORM_IMPL_SPDY_MEM_SLICE_IMPL_H_

#include "base/memory/ref_counted.h"
#include "net/base/io_buffer.h"
#include "net/third_party/quiche/src/spdy/platform/api/spdy_export.h"

namespace spdy {

// SpdyMemSliceImpl wraps a reference counted MemSlice and only provides partial
// interfaces of MemSlice.
class SPDY_EXPORT_PRIVATE SpdyMemSliceImpl {
 public:
  // Constructs an empty SpdyMemSliceImpl that contains an empty MemSlice.
  SpdyMemSliceImpl();

  // Constructs a SpdyMemSliceImpl by adding a reference to the data held in
  // |io_buffer|, which should be passed by value.
  explicit SpdyMemSliceImpl(scoped_refptr<net::IOBufferWithSize> io_buffer);

  // Constructs a SpdyMemSliceImpl with reference count 1 to a newly allocated
  // data buffer of |length| bytes.
  explicit SpdyMemSliceImpl(size_t length);

  SpdyMemSliceImpl(const SpdyMemSliceImpl& other) = delete;
  SpdyMemSliceImpl& operator=(const SpdyMemSliceImpl& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  SpdyMemSliceImpl(SpdyMemSliceImpl&& other);
  SpdyMemSliceImpl& operator=(SpdyMemSliceImpl&& other);

  ~SpdyMemSliceImpl();

  // Returns a char pointer to underlying data buffer.
  const char* data() const;
  // Returns the length of underlying data buffer.
  size_t length() const;

 private:
  scoped_refptr<net::IOBufferWithSize> io_buffer_;
};

}  // namespace spdy

#endif  // NET_SPDY_PLATFORM_IMPL_SPDY_MEM_SLICE_IMPL_H_
