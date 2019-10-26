// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/mock_clock.h"

namespace quic {

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

}  // namespace quic
