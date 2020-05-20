// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_BLOCKED_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_BLOCKED_FRAME_H_

#include <ostream>

#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

// The BLOCKED frame is used to indicate to the remote endpoint that this
// endpoint believes itself to be flow-control blocked but otherwise ready to
// send data. The BLOCKED frame is purely advisory and optional.
// Based on SPDY's BLOCKED frame (undocumented as of 2014-01-28).
struct QUIC_EXPORT_PRIVATE QuicBlockedFrame {
  QuicBlockedFrame();
  QuicBlockedFrame(QuicControlFrameId control_frame_id, QuicStreamId stream_id);
  QuicBlockedFrame(QuicControlFrameId control_frame_id,
                   QuicStreamId stream_id,
                   QuicStreamOffset offset);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicBlockedFrame& b);

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id;

  // 0 is a special case meaning the connection is blocked, rather than a
  // stream.  So stream_id 0 corresponds to a BLOCKED frame and non-0
  // corresponds to a STREAM_BLOCKED.
  // TODO(fkastenholz): This should be converted to use
  // QuicUtils::GetInvalidStreamId to get the correct invalid stream id value
  // and not rely on 0.
  QuicStreamId stream_id;

  // For Google QUIC, the offset is ignored.
  QuicStreamOffset offset;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_BLOCKED_FRAME_H_
