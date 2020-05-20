// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_CLOCK_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_CLOCK_H_

#include "net/third_party/quiche/src/quic/core/quic_clock.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"

namespace quic {

class MockClock : public QuicClock {
 public:
  MockClock();
  MockClock(const MockClock&) = delete;
  MockClock& operator=(const MockClock&) = delete;
  ~MockClock() override;

  // QuicClock implementation:
  QuicTime Now() const override;
  QuicTime ApproximateNow() const override;
  QuicWallTime WallNow() const override;

  // Advances the current time by |delta|, which may be negative.
  void AdvanceTime(QuicTime::Delta delta);

 private:
  QuicTime now_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_CLOCK_H_
