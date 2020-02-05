// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_WINDOW_UPDATE_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_WINDOW_UPDATE_FRAME_H_

#include <ostream>

#include "net/third_party/quiche/src/quic/core/quic_types.h"

namespace quic {

// Flow control updates per-stream and at the connection level.
// Based on SPDY's WINDOW_UPDATE frame, but uses an absolute max data bytes
// rather than a window delta.
struct QUIC_EXPORT_PRIVATE QuicWindowUpdateFrame {
  QuicWindowUpdateFrame();
  QuicWindowUpdateFrame(QuicControlFrameId control_frame_id,
                        QuicStreamId stream_id,
                        QuicByteCount max_data);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicWindowUpdateFrame& w);

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id;

  // The stream this frame applies to.  0 is a special case meaning the overall
  // connection rather than a specific stream.
  QuicStreamId stream_id;

  // Maximum data allowed in the stream or connection. The receiver of this
  // frame must not send data which would exceedes this restriction.
  QuicByteCount max_data;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_WINDOW_UPDATE_FRAME_H_
