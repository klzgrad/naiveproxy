// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_epoll_clock.h"

#include "quiche/common/platform/api/quiche_flag_utils.h"

namespace quic {

QuicEpollClock::QuicEpollClock(QuicEpollServer* epoll_server)
    : epoll_server_(epoll_server), largest_time_(QuicTime::Zero()) {}

QuicEpollClock::~QuicEpollClock() {}

QuicTime QuicEpollClock::ApproximateNow() const {
  return CreateTimeFromMicroseconds(epoll_server_->ApproximateNowInUsec());
}

QuicTime QuicEpollClock::Now() const {
  QuicTime now = CreateTimeFromMicroseconds(epoll_server_->NowInUsec());

  if (now <= largest_time_) {
    if (now < largest_time_) {
      QUICHE_CODE_COUNT(quic_epoll_clock_step_backward);
    }
    // Time not increasing, return |largest_time_|.
    return largest_time_;
  }

  largest_time_ = now;
  return largest_time_;
}

QuicWallTime QuicEpollClock::WallNow() const {
  return QuicWallTime::FromUNIXMicroseconds(
      epoll_server_->ApproximateNowInUsec());
}

QuicTime QuicEpollClock::ConvertWallTimeToQuicTime(
    const QuicWallTime& walltime) const {
  return QuicTime::Zero() +
         QuicTime::Delta::FromMicroseconds(walltime.ToUNIXMicroseconds());
}

}  // namespace quic
