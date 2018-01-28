// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_CHROMIUM_CLOCK_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_CHROMIUM_CLOCK_H_

#include "base/time/time.h"
#include "net/quic/platform/api/quic_clock.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// Clock to efficiently retrieve an approximately accurate time from an
// EpollServer.
class QUIC_EXPORT_PRIVATE QuicChromiumClock : public QuicClock {
 public:
  static QuicChromiumClock* GetInstance();

  QuicChromiumClock();
  ~QuicChromiumClock() override;

  // QuicClock implementation:
  QuicTime ApproximateNow() const override;
  QuicTime Now() const override;
  QuicWallTime WallNow() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicChromiumClock);
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_CHROMIUM_CLOCK_H_
