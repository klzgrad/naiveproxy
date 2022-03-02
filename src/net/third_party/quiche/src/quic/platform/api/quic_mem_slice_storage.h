// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_STORAGE_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_STORAGE_H_

#include <vector>

#include "absl/types/span.h"
#include "quic/core/quic_buffer_allocator.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_export.h"
#include "quic/platform/api/quic_iovec.h"
#include "quic/platform/api/quic_mem_slice.h"

namespace quic {

// QuicMemSliceStorage is a container class that store QuicMemSlices for further
// use cases such as turning into QuicMemSliceSpan.
class QUIC_EXPORT_PRIVATE QuicMemSliceStorage {
 public:
  QuicMemSliceStorage(const struct iovec* iov, int iov_count,
                      QuicBufferAllocator* allocator,
                      const QuicByteCount max_slice_len);

  QuicMemSliceStorage(const QuicMemSliceStorage& other) = default;
  QuicMemSliceStorage& operator=(const QuicMemSliceStorage& other) = default;
  QuicMemSliceStorage(QuicMemSliceStorage&& other) = default;
  QuicMemSliceStorage& operator=(QuicMemSliceStorage&& other) = default;

  ~QuicMemSliceStorage() = default;

  // Return a QuicMemSliceSpan form of the storage.
  absl::Span<QuicMemSlice> ToSpan() { return absl::MakeSpan(storage_); }

 private:
  std::vector<QuicMemSlice> storage_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_MEM_SLICE_STORAGE_H_
