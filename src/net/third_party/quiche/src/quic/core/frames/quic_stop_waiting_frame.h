// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_STOP_WAITING_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_STOP_WAITING_FRAME_H_

#include <ostream>

#include "net/third_party/quiche/src/quic/core/frames/quic_inlined_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicStopWaitingFrame
    : public QuicInlinedFrame<QuicStopWaitingFrame> {
  QuicStopWaitingFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicStopWaitingFrame& s);

  // The lowest packet we've sent which is unacked, and we expect an ack for.
  QuicPacketNumber least_unacked;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_STOP_WAITING_FRAME_H_
