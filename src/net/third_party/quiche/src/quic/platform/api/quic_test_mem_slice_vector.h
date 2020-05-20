// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TEST_MEM_SLICE_VECTOR_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TEST_MEM_SLICE_VECTOR_H_

#include <utility>

#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_span.h"
#include "net/quic/platform/impl/quic_test_mem_slice_vector_impl.h"

namespace quic {
namespace test {
// QuicTestMemSliceVector is a test only class which creates a vector of
// platform-specific data structure (used as QuicMemSlice) from an array of data
// buffers. QuicTestMemSliceVector does not own the underlying data buffer.
// Tests using QuicTestMemSliceVector need to make sure the actual data buffers
// outlive QuicTestMemSliceVector, and QuicTestMemSliceVector outlive the
// returned QuicMemSliceSpan.
class QUIC_NO_EXPORT QuicTestMemSliceVector {
 public:
  explicit QuicTestMemSliceVector(std::vector<std::pair<char*, size_t>> buffers)
      : impl_(std::move(buffers)) {}

  QuicMemSliceSpan span() { return QuicMemSliceSpan(impl_.span()); }

 private:
  QuicTestMemSliceVectorImpl impl_;
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TEST_MEM_SLICE_VECTOR_H_
