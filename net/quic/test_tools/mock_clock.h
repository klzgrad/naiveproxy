// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_MOCK_CLOCK_H_
#define NET_QUIC_TEST_TOOLS_MOCK_CLOCK_H_

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/time/time.h"
#include "net/quic/platform/api/quic_clock.h"

namespace net {

class MockClock : public QuicClock {
 public:
  MockClock();
  ~MockClock() override;

  // QuicClock implementation:
  QuicTime Now() const override;
  QuicTime ApproximateNow() const override;
  QuicWallTime WallNow() const override;

  // Advances the current time by |delta|, which may be negative.
  void AdvanceTime(QuicTime::Delta delta);

  // Returns the current time in ticks.
  base::TimeTicks NowInTicks() const;

 private:
  QuicTime now_;

  DISALLOW_COPY_AND_ASSIGN(MockClock);
};

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_MOCK_CLOCK_H_
