// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_clock.h"

#include <limits>

#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

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

}  // namespace quic
