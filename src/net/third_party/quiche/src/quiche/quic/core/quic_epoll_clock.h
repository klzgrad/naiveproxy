// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_EPOLL_CLOCK_H_
#define QUICHE_QUIC_CORE_QUIC_EPOLL_CLOCK_H_

#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_epoll.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// Clock to efficiently retrieve an approximately accurate time from an
// EpollServer.
class QUIC_EXPORT_PRIVATE QuicEpollClock : public QuicClock {
 public:
  explicit QuicEpollClock(QuicEpollServer* epoll_server);
  QuicEpollClock(const QuicEpollClock&) = delete;
  QuicEpollClock& operator=(const QuicEpollClock&) = delete;
  ~QuicEpollClock() override;

  // Returns the approximate current time as a QuicTime object.
  QuicTime ApproximateNow() const override;

  // Returns the current time as a QuicTime object.
  // Note: this use significant resources please use only if needed.
  QuicTime Now() const override;

  // WallNow returns the current wall-time - a time that is consistent across
  // different clocks.
  QuicWallTime WallNow() const override;

  // Override to do less work in this implementation.  The epoll clock is
  // already based on system (unix epoch) time, no conversion required.
  QuicTime ConvertWallTimeToQuicTime(
      const QuicWallTime& walltime) const override;

 protected:
  QuicEpollServer* epoll_server_;
  // Largest time returned from Now() so far.
  mutable QuicTime largest_time_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_EPOLL_CLOCK_H_
