// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/mock_clock.h"

namespace net {

MockClock::MockClock() : now_(QuicTime::Zero()) {}

MockClock::~MockClock() {}

void MockClock::AdvanceTime(QuicTime::Delta delta) {
  now_ = now_ + delta;
}

QuicTime MockClock::Now() const {
  return now_;
}

QuicTime MockClock::ApproximateNow() const {
  return now_;
}

QuicWallTime MockClock::WallNow() const {
  return QuicWallTime::FromUNIXSeconds((now_ - QuicTime::Zero()).ToSeconds());
}

base::TimeTicks MockClock::NowInTicks() const {
  base::TimeTicks ticks;
  return ticks + base::TimeDelta::FromMicroseconds(
                     (now_ - QuicTime::Zero()).ToMicroseconds());
}

}  // namespace net
