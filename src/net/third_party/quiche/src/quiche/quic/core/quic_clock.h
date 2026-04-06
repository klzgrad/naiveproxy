// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CLOCK_H_
#define QUICHE_QUIC_CORE_QUIC_CLOCK_H_

#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_export.h"

/* API_DESCRIPTION
 QuicClock is used by QUIC core to get current time. Its instance is created by
 applications and passed into QuicDispatcher and QuicConnectionHelperInterface.
 API-DESCRIPTION */

namespace quic {

// Interface for retrieving the current time.
class QUICHE_EXPORT QuicClock {
 public:
  QuicClock() = default;
  virtual ~QuicClock() = default;

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

 protected:
  // Creates a new QuicTime using |time_us| as the internal value.
  QuicTime CreateTimeFromMicroseconds(uint64_t time_us) const {
    return QuicTime(time_us);
  }
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CLOCK_H_
