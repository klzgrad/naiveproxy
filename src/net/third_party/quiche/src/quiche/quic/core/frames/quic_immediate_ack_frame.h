// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_IMMEDIATE_ACK_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_IMMEDIATE_ACK_FRAME_H_

#include <ostream>

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// A frame that allows the sender to request an immediate ack from the receiver.
// Not a retransmittable frame.
struct QUICHE_EXPORT QuicImmediateAckFrame
    : QuicInlinedFrame<QuicImmediateAckFrame> {
  QuicImmediateAckFrame();

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicImmediateAckFrame& immediate_ack_frame);

  QuicFrameType type;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_IMMEDIATE_ACK_FRAME_H_
