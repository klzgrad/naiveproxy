// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_IOVECTOR_H_
#define NET_QUIC_CORE_QUIC_IOVECTOR_H_

#include <cstddef>

#include "net/base/iovec.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// Convenience wrapper to wrap an iovec array and the total length, which must
// be less than or equal to the actual total length of the iovecs.
struct QUIC_EXPORT_PRIVATE QuicIOVector {
  QuicIOVector(const struct iovec* iov, int iov_count, size_t total_length)
      : iov(iov), iov_count(iov_count), total_length(total_length) {}

  const struct iovec* iov;
  const int iov_count;
  const size_t total_length;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_IOVECTOR_H_
