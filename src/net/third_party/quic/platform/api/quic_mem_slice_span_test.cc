// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_mem_slice_span.h"

#include "net/third_party/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quic/core/quic_stream_send_buffer.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_test_mem_slice_vector.h"

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

TEST_F(QuicMemSliceSpanImplTest, SaveDataInSendBuffer) {
  SimpleBufferAllocator allocator;
  QuicStreamSendBuffer send_buffer(&allocator);
  QuicTestMemSliceVector vector(buffers_);

  EXPECT_EQ(10 * 1024u, vector.span().SaveMemSlicesInSendBuffer(&send_buffer));
  EXPECT_EQ(10u, send_buffer.size());
}

TEST_F(QuicMemSliceSpanImplTest, SaveEmptyMemSliceInSendBuffer) {
  SimpleBufferAllocator allocator;
  QuicStreamSendBuffer send_buffer(&allocator);
  buffers_.push_back(std::make_pair(nullptr, 0));
  QuicTestMemSliceVector vector(buffers_);
  EXPECT_EQ(10 * 1024u, vector.span().SaveMemSlicesInSendBuffer(&send_buffer));
  // Verify the empty slice does not get saved.
  EXPECT_EQ(10u, send_buffer.size());
}

}  // namespace
}  // namespace test
}  // namespace quic
