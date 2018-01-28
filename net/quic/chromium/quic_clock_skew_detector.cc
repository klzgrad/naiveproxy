// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/chromium/quic_clock_skew_detector.h"

#include "base/metrics/histogram_macros.h"

namespace net {

QuicClockSkewDetector::QuicClockSkewDetector(base::TimeTicks ticks_time,
                                             base::Time wall_time)
    : last_ticks_time_(ticks_time), last_wall_time_(wall_time) {}

bool QuicClockSkewDetector::ClockSkewDetected(base::TimeTicks ticks_now,
                                              base::Time wall_now) {
  base::TimeDelta ticks_delta = ticks_now - last_ticks_time_;
  base::TimeDelta wall_delta = wall_now - last_wall_time_;
  base::TimeDelta offset = wall_delta - ticks_delta;
  last_wall_time_ = wall_now;
  last_ticks_time_ = ticks_now;

  UMA_HISTOGRAM_TIMES(
      "Net.QuicClock.SkewOffset",
      base::TimeDelta::FromMicroseconds(offset.InMicroseconds()));

  if (offset < base::TimeDelta::FromSeconds(1))
    return false;

  return true;
}

}  // namespace net
