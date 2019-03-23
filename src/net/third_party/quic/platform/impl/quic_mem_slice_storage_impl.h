// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_MEM_SLICE_STORAGE_IMPL_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_MEM_SLICE_STORAGE_IMPL_H_

#include "base/memory/ref_counted.h"
#include "net/base/io_buffer.h"
#include "net/third_party/quic/core/quic_buffer_allocator.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_mem_slice_span.h"
#include "net/third_party/quic/platform/impl/quic_iovec_impl.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicMemSliceStorageImpl {
 public:
  QuicMemSliceStorageImpl(const struct iovec* iov,
                          int iov_count,
                          QuicBufferAllocator* allocator,
                          const QuicByteCount max_slice_len);

  QuicMemSliceStorageImpl(const QuicMemSliceStorageImpl& other) = default;
  QuicMemSliceStorageImpl& operator=(const QuicMemSliceStorageImpl& other) =
      default;
  QuicMemSliceStorageImpl(QuicMemSliceStorageImpl&& other) = default;
  QuicMemSliceStorageImpl& operator=(QuicMemSliceStorageImpl&& other) = default;

  ~QuicMemSliceStorageImpl() = default;

  QuicMemSliceSpan ToSpan() {
    return QuicMemSliceSpan(QuicMemSliceSpanImpl(
        buffers_.data(), lengths_.data(), buffers_.size()));
  }

 private:
  std::vector<scoped_refptr<net::IOBuffer>> buffers_;
  std::vector<int> lengths_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_IMPL_QUIC_MEM_SLICE_STORAGE_IMPL_H_
