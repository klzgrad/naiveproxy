// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_CLOCK_H_
#define NET_QUIC_PLATFORM_API_QUIC_CLOCK_H_

#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// Interface for retreiving the current time.
class QUIC_EXPORT_PRIVATE QuicClock {
 public:
  QuicClock();
  virtual ~QuicClock();

  QuicClock(const QuicClock&) = delete;
  QuicClock& operator=(const QuicClock&) = delete;

  // Returns the approximate current time as a QuicTime object.
  virtual QuicTime ApproximateNow() const = 0;

  // Returns the current time as a QuicTime object.
  // Note: this use significant resources please use only if needed.
  virtual QuicTime Now() const = 0;

  // WallNow returns the current wall-time - a time that is consistent across
  // different clocks.
  virtual QuicWallTime WallNow() const = 0;

  // Converts |walltime| to a QuicTime relative to this clock's epoch.
  virtual QuicTime ConvertWallTimeToQuicTime(
      const QuicWallTime& walltime) const;

 protected:
  // Creates a new QuicTime using |time_us| as the internal value.
  QuicTime CreateTimeFromMicroseconds(uint64_t time_us) const {
    return QuicTime(time_us);
  }
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_CLOCK_H_
