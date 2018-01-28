// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_WINDOW_UPDATE_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_WINDOW_UPDATE_FRAME_H_

#include <ostream>

#include "net/quic/core/frames/quic_control_frame.h"

namespace net {

// Flow control updates per-stream and at the connection level.
// Based on SPDY's WINDOW_UPDATE frame, but uses an absolute byte offset rather
// than a window delta.
// TODO(rjshade): A possible future optimization is to make stream_id and
//                byte_offset variable length, similar to stream frames.
struct QUIC_EXPORT_PRIVATE QuicWindowUpdateFrame : public QuicControlFrame {
  QuicWindowUpdateFrame();
  QuicWindowUpdateFrame(QuicControlFrameId control_frame_id,
                        QuicStreamId stream_id,
                        QuicStreamOffset byte_offset);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicWindowUpdateFrame& w);

  // The stream this frame applies to.  0 is a special case meaning the overall
  // connection rather than a specific stream.
  QuicStreamId stream_id;

  // Byte offset in the stream or connection. The receiver of this frame must
  // not send data which would result in this offset being exceeded.
  QuicStreamOffset byte_offset;
};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_WINDOW_UPDATE_FRAME_H_
