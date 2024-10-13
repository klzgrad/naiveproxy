// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_buffer_allocator.h"

#include <algorithm>
#include <cstring>

#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_prefetch.h"

namespace quiche {

QuicheBuffer QuicheBuffer::CopyFromIovec(QuicheBufferAllocator* allocator,
                                         const struct iovec* iov, int iov_count,
                                         size_t iov_offset,
                                         size_t buffer_length) {
  if (buffer_length == 0) {
    return {};
  }

  int iovnum = 0;
  while (iovnum < iov_count && iov_offset >= iov[iovnum].iov_len) {
    iov_offset -= iov[iovnum].iov_len;
    ++iovnum;
  }
  QUICHE_DCHECK_LE(iovnum, iov_count);
  if (iovnum >= iov_count) {
    QUICHE_BUG(quiche_bug_10839_1)
        << "iov_offset larger than iovec total size.";
    return {};
  }
  QUICHE_DCHECK_LE(iov_offset, iov[iovnum].iov_len);

  // Unroll the first iteration that handles iov_offset.
  const size_t iov_available = iov[iovnum].iov_len - iov_offset;
  size_t copy_len = std::min(buffer_length, iov_available);

  // Try to prefetch the next iov if there is at least one more after the
  // current. Otherwise, it looks like an irregular access that the hardware
  // prefetcher won't speculatively prefetch. Only prefetch one iov because
  // generally, the iov_offset is not 0, input iov consists of 2K buffers and
  // the output buffer is ~1.4K.
  if (copy_len == iov_available && iovnum + 1 < iov_count) {
    char* next_base = static_cast<char*>(iov[iovnum + 1].iov_base);
    // Prefetch 2 cachelines worth of data to get the prefetcher started; leave
    // it to the hardware prefetcher after that.
    quiche::QuichePrefetchT0(next_base);
    if (iov[iovnum + 1].iov_len >= 64) {
      quiche::QuichePrefetchT0(next_base + ABSL_CACHELINE_SIZE);
    }
  }

  QuicheBuffer buffer(allocator, buffer_length);

  const char* src = static_cast<char*>(iov[iovnum].iov_base) + iov_offset;
  char* dst = buffer.data();
  while (true) {
    memcpy(dst, src, copy_len);
    buffer_length -= copy_len;
    dst += copy_len;
    if (buffer_length == 0 || ++iovnum >= iov_count) {
      break;
    }
    src = static_cast<char*>(iov[iovnum].iov_base);
    copy_len = std::min(buffer_length, iov[iovnum].iov_len);
  }

  QUICHE_BUG_IF(quiche_bug_10839_2, buffer_length > 0)
      << "iov_offset + buffer_length larger than iovec total size.";

  return buffer;
}

}  // namespace quiche
