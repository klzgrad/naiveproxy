// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_BLOCKED_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_BLOCKED_FRAME_H_

#include <ostream>

#include "net/quic/core/frames/quic_control_frame.h"

namespace net {

// The BLOCKED frame is used to indicate to the remote endpoint that this
// endpoint believes itself to be flow-control blocked but otherwise ready to
// send data. The BLOCKED frame is purely advisory and optional.
// Based on SPDY's BLOCKED frame (undocumented as of 2014-01-28).
struct QUIC_EXPORT_PRIVATE QuicBlockedFrame : public QuicControlFrame {
  QuicBlockedFrame();
  QuicBlockedFrame(QuicControlFrameId control_frame_id, QuicStreamId stream_id);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicBlockedFrame& b);

  // The stream this frame applies to.  0 is a special case meaning the overall
  // connection rather than a specific stream.
  QuicStreamId stream_id;
};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_BLOCKED_FRAME_H_
