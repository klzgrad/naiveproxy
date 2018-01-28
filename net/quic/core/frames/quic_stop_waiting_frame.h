// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_STOP_WAITING_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_STOP_WAITING_FRAME_H_

#include <ostream>

#include "net/quic/core/quic_types.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

struct QUIC_EXPORT_PRIVATE QuicStopWaitingFrame {
  QuicStopWaitingFrame();
  ~QuicStopWaitingFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicStopWaitingFrame& s);

  // The lowest packet we've sent which is unacked, and we expect an ack for.
  QuicPacketNumber least_unacked;
};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_STOP_WAITING_FRAME_H_
