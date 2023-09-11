// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_STREAMS_BLOCKED_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_STREAMS_BLOCKED_FRAME_H_

#include <ostream>

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// IETF format STREAMS_BLOCKED frame.
// The sender uses this to inform the peer that the sender wished to
// open a new stream, exceeding the limit on the number of streams.
struct QUIC_EXPORT_PRIVATE QuicStreamsBlockedFrame
    : public QuicInlinedFrame<QuicStreamsBlockedFrame> {
  QuicStreamsBlockedFrame();
  QuicStreamsBlockedFrame(QuicControlFrameId control_frame_id,
                          QuicStreamCount stream_count, bool unidirectional);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os, const QuicStreamsBlockedFrame& frame);

  QuicFrameType type;

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;

  // The number of streams that the sender wishes to exceed
  QuicStreamCount stream_count = 0;

  // Whether uni- or bi-directional streams
  bool unidirectional = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_STREAMS_BLOCKED_FRAME_H_
