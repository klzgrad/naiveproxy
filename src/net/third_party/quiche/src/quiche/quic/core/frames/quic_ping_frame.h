// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_PING_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_PING_FRAME_H_

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A ping frame contains no payload, though it is retransmittable,
// and ACK'd just like other normal frames.
struct QUICHE_EXPORT QuicPingFrame : public QuicInlinedFrame<QuicPingFrame> {
  QuicPingFrame();
  explicit QuicPingFrame(QuicControlFrameId control_frame_id);

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicPingFrame& ping_frame);

  QuicFrameType type;

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_PING_FRAME_H_
