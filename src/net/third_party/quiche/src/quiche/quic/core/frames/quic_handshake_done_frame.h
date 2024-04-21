// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_HANDSHAKE_DONE_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_HANDSHAKE_DONE_FRAME_H_

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A HANDSHAKE_DONE frame contains no payload, and it is retransmittable,
// and ACK'd just like other normal frames.
struct QUICHE_EXPORT QuicHandshakeDoneFrame
    : public QuicInlinedFrame<QuicHandshakeDoneFrame> {
  QuicHandshakeDoneFrame();
  explicit QuicHandshakeDoneFrame(QuicControlFrameId control_frame_id);

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicHandshakeDoneFrame& handshake_done_frame);

  QuicFrameType type;

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_HANDSHAKE_DONE_FRAME_H_
