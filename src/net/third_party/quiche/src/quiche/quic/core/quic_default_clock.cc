// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_default_clock.h"

#include "absl/time/clock.h"

namespace quic {

QuicDefaultClock* QuicDefaultClock::Get() {
  static QuicDefaultClock* clock = new QuicDefaultClock();
  return clock;
}

QuicTime QuicDefaultClock::ApproximateNow() const { return Now(); }

QuicTime QuicDefaultClock::Now() const {
  return CreateTimeFromMicroseconds(absl::GetCurrentTimeNanos() / 1000);
}

QuicWallTime QuicDefaultClock::WallNow() const {
  return QuicWallTime::FromUNIXMicroseconds(absl::GetCurrentTimeNanos() / 1000);
}

}  // namespace quic
