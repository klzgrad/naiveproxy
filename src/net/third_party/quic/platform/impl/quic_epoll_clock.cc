// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/quic_epoll_clock.h"

#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/tools/epoll_server/epoll_server.h"

namespace quic {

QuicEpollClock::QuicEpollClock(net::EpollServer* epoll_server)
    : epoll_server_(epoll_server), largest_time_(QuicTime::Zero()) {}

QuicEpollClock::~QuicEpollClock() {}

QuicTime QuicEpollClock::ApproximateNow() const {
  return CreateTimeFromMicroseconds(epoll_server_->ApproximateNowInUsec());
}

QuicTime QuicEpollClock::Now() const {
  QuicTime now = CreateTimeFromMicroseconds(epoll_server_->NowInUsec());
  if (!GetQuicReloadableFlag(quic_monotonic_epoll_clock)) {
    return now;
  }

  if (now <= largest_time_) {
    if (now < largest_time_) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_monotonic_epoll_clock);
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
