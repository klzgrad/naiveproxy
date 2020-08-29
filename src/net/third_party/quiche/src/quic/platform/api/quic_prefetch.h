// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_PREFETCH_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_PREFETCH_H_

#include "net/quic/platform/impl/quic_prefetch_impl.h"

namespace quic {

// Move data into the cache before it is read, or "prefetch" it.
//
// The value of `addr` is the address of the memory to prefetch. If
// the target and compiler support it, data prefetch instructions are
// generated. If the prefetch is done some time before the memory is
// read, it may be in the cache by the time the read occurs.
//
// The function names specify the temporal locality heuristic applied,
// using the names of Intel prefetch instructions:
//
//   T0 - high degree of temporal locality; data should be left in as
//        many levels of the cache possible
//   T1 - moderate degree of temporal locality
//   T2 - low degree of temporal locality
//   Nta - no temporal locality, data need not be left in the cache
//         after the read
//
// Incorrect or gratuitous use of these functions can degrade
// performance, so use them only when representative benchmarks show
// an improvement.

inline void QuicPrefetchT0(const void* addr) {
  return QuicPrefetchT0Impl(addr);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_PREFETCH_H_
