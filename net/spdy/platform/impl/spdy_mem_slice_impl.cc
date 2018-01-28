// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/spdy/platform/impl/spdy_mem_slice_impl.h"

#include <utility>

namespace net {

SpdyMemSliceImpl::SpdyMemSliceImpl() = default;

SpdyMemSliceImpl::SpdyMemSliceImpl(scoped_refptr<IOBufferWithSize> io_buffer)
    : io_buffer_(std::move(io_buffer)) {}

SpdyMemSliceImpl::SpdyMemSliceImpl(size_t length)
    : io_buffer_(new IOBufferWithSize(length)) {}

SpdyMemSliceImpl::SpdyMemSliceImpl(SpdyMemSliceImpl&& other)
    : io_buffer_(std::move(other.io_buffer_)) {
  other.io_buffer_ = nullptr;
}

SpdyMemSliceImpl& SpdyMemSliceImpl::operator=(SpdyMemSliceImpl&& other) {
  io_buffer_ = std::move(other.io_buffer_);
  other.io_buffer_ = nullptr;
  return *this;
}

SpdyMemSliceImpl::~SpdyMemSliceImpl() = default;

const char* SpdyMemSliceImpl::data() const {
  return io_buffer_ != nullptr ? io_buffer_->data() : nullptr;
}

size_t SpdyMemSliceImpl::length() const {
  return io_buffer_ != nullptr ? io_buffer_->size() : 0;
}

}  // namespace net
