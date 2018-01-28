// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_test_mem_slice_vector_impl.h"

namespace net {
namespace test {

TestIOBuffer::~TestIOBuffer() {
  data_ = nullptr;
}

QuicTestMemSliceVectorImpl::~QuicTestMemSliceVectorImpl() {}

QuicTestMemSliceVectorImpl::QuicTestMemSliceVectorImpl(
    std::vector<std::pair<char*, int>> buffers) {
  for (auto& buffer : buffers) {
    buffers_.push_back(new TestIOBuffer(buffer.first));
    lengths_.push_back(buffer.second);
  }
}

QuicMemSliceSpanImpl QuicTestMemSliceVectorImpl::span() {
  return QuicMemSliceSpanImpl(buffers_.data(), lengths_.data(),
                              buffers_.size());
}

}  // namespace test
}  // namespace net
