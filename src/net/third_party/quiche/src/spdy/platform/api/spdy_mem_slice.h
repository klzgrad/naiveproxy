// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_PLATFORM_API_SPDY_MEM_SLICE_H_
#define QUICHE_SPDY_PLATFORM_API_SPDY_MEM_SLICE_H_

#include <utility>

#include "net/third_party/quiche/src/common/platform/api/quiche_export.h"
#include "net/spdy/platform/impl/spdy_mem_slice_impl.h"

namespace spdy {

// SpdyMemSlice is an internally reference counted data buffer used as the
// source buffers for write operations. SpdyMemSlice implicitly maintains a
// reference count and will free the underlying data buffer when the reference
// count reaches zero.
class QUICHE_EXPORT_PRIVATE SpdyMemSlice {
 public:
  // Constructs an empty SpdyMemSlice with no underlying data and 0 reference
  // count.
  SpdyMemSlice() = default;

  // Constructs a SpdyMemSlice with reference count 1 to a newly allocated data
  // buffer of |length| bytes.
  explicit SpdyMemSlice(size_t length) : impl_(length) {}

  // Constructs a SpdyMemSlice from |impl|. It takes the reference away from
  // |impl|.
  explicit SpdyMemSlice(SpdyMemSliceImpl impl) : impl_(std::move(impl)) {}

  SpdyMemSlice(const SpdyMemSlice& other) = delete;
  SpdyMemSlice& operator=(const SpdyMemSlice& other) = delete;

  // Move constructors. |other| will not hold a reference to the data buffer
  // after this call completes.
  SpdyMemSlice(SpdyMemSlice&& other) = default;
  SpdyMemSlice& operator=(SpdyMemSlice&& other) = default;

  ~SpdyMemSlice() = default;

  // Returns a char pointer to underlying data buffer.
  const char* data() const { return impl_.data(); }
  // Returns the length of underlying data buffer.
  size_t length() const { return impl_.length(); }

 private:
  SpdyMemSliceImpl impl_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_PLATFORM_API_SPDY_MEM_SLICE_H_
