// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_mem_slice_storage.h"
#include "net/third_party/quic/core/quic_simple_buffer_allocator.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicMemSliceStorageImplTest : public QuicTest {
 public:
  QuicMemSliceStorageImplTest() = default;
};

TEST_F(QuicMemSliceStorageImplTest, EmptyIov) {
  QuicMemSliceStorage storage(nullptr, 0, nullptr, 1024);
  EXPECT_TRUE(storage.ToSpan().empty());
}

TEST_F(QuicMemSliceStorageImplTest, SingleIov) {
  SimpleBufferAllocator allocator;
  QuicString body(3, 'c');
  struct iovec iov = {const_cast<char*>(body.data()), body.length()};
  QuicMemSliceStorage storage(&iov, 1, &allocator, 1024);
  auto span = storage.ToSpan();
  EXPECT_EQ("ccc", span.GetData(0));
  EXPECT_NE(static_cast<const void*>(span.GetData(0).data()), body.data());
}

TEST_F(QuicMemSliceStorageImplTest, MultipleIovInSingleSlice) {
  SimpleBufferAllocator allocator;
  QuicString body1(3, 'a');
  QuicString body2(4, 'b');
  struct iovec iov[] = {{const_cast<char*>(body1.data()), body1.length()},
                        {const_cast<char*>(body2.data()), body2.length()}};

  QuicMemSliceStorage storage(iov, 2, &allocator, 1024);
  auto span = storage.ToSpan();
  EXPECT_EQ("aaabbbb", span.GetData(0));
}

TEST_F(QuicMemSliceStorageImplTest, MultipleIovInMultipleSlice) {
  SimpleBufferAllocator allocator;
  QuicString body1(4, 'a');
  QuicString body2(4, 'b');
  struct iovec iov[] = {{const_cast<char*>(body1.data()), body1.length()},
                        {const_cast<char*>(body2.data()), body2.length()}};

  QuicMemSliceStorage storage(iov, 2, &allocator, 4);
  auto span = storage.ToSpan();
  EXPECT_EQ("aaaa", span.GetData(0));
  EXPECT_EQ("bbbb", span.GetData(1));
}

}  // namespace
}  // namespace test
}  // namespace quic
