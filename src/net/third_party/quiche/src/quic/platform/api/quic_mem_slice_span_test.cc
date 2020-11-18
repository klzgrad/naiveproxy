// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_span.h"

#include "net/third_party/quiche/src/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test_mem_slice_vector.h"

namespace quic {
namespace test {
namespace {

class QuicMemSliceSpanImplTest : public QuicTest {
 public:
  QuicMemSliceSpanImplTest() {
    for (size_t i = 0; i < 10; ++i) {
      buffers_.push_back(std::make_pair(data_, 1024));
    }
  }

  char data_[1024];
  std::vector<std::pair<char*, size_t>> buffers_;
};

TEST_F(QuicMemSliceSpanImplTest, ConsumeAll) {
  SimpleBufferAllocator allocator;
  QuicTestMemSliceVector vector(buffers_);

  int num_slices = 0;
  QuicByteCount bytes_consumed =
      vector.span().ConsumeAll([&](QuicMemSlice slice) {
        EXPECT_EQ(data_, slice.data());
        EXPECT_EQ(1024u, slice.length());
        ++num_slices;
      });

  EXPECT_EQ(10 * 1024u, bytes_consumed);
  EXPECT_EQ(10, num_slices);
}

}  // namespace
}  // namespace test
}  // namespace quic
