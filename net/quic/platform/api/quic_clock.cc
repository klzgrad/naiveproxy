// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_clock.h"

namespace net {

QuicClock::QuicClock() {}

QuicClock::~QuicClock() {}

QuicTime QuicClock::ConvertWallTimeToQuicTime(
    const QuicWallTime& walltime) const {
  //     ..........................
  //     |            |           |
  // unix epoch   |walltime|   WallNow()
  //     ..........................
  //            |     |           |
  //     clock epoch  |         Now()
  //               result
  //
  // result = Now() - (WallNow() - walltime)
  return Now() - QuicTime::Delta::FromMicroseconds(
                     WallNow()
                         .Subtract(QuicTime::Delta::FromMicroseconds(
                             walltime.ToUNIXMicroseconds()))
                         .ToUNIXMicroseconds());
}

}  // namespace net
