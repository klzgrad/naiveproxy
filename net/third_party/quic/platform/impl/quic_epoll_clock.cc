// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/quic_epoll_clock.h"

#include "net/tools/epoll_server/epoll_server.h"

namespace quic {

QuicEpollClock::QuicEpollClock(net::EpollServer* epoll_server)
    : epoll_server_(epoll_server) {}

QuicEpollClock::~QuicEpollClock() = default;

QuicTime QuicEpollClock::ApproximateNow() const {
  return QuicTime::Zero() + QuicTime::Delta::FromMicroseconds(
                                epoll_server_->ApproximateNowInUsec());
}

QuicTime QuicEpollClock::Now() const {
  return QuicTime::Zero() +
         QuicTime::Delta::FromMicroseconds(epoll_server_->NowInUsec());
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
