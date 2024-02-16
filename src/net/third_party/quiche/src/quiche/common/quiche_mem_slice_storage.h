// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_MEM_SLICE_STORAGE_H_
#define QUICHE_COMMON_QUICHE_MEM_SLICE_STORAGE_H_

#include <vector>

#include "absl/types/span.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_iovec.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace quiche {

// QuicheMemSliceStorage is a container class that store QuicheMemSlices for
// further use cases such as turning into QuicheMemSliceSpan.
class QUICHE_EXPORT QuicheMemSliceStorage {
 public:
  QuicheMemSliceStorage(const struct iovec* iov, int iov_count,
                        QuicheBufferAllocator* allocator,
                        const quic::QuicByteCount max_slice_len);

  QuicheMemSliceStorage(const QuicheMemSliceStorage& other) = delete;
  QuicheMemSliceStorage& operator=(const QuicheMemSliceStorage& other) = delete;
  QuicheMemSliceStorage(QuicheMemSliceStorage&& other) = default;
  QuicheMemSliceStorage& operator=(QuicheMemSliceStorage&& other) = default;

  ~QuicheMemSliceStorage() = default;

  // Return a QuicheMemSliceSpan form of the storage.
  absl::Span<QuicheMemSlice> ToSpan() { return absl::MakeSpan(storage_); }

 private:
  std::vector<QuicheMemSlice> storage_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_MEM_SLICE_STORAGE_H_
