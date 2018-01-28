// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/frames/quic_blocked_frame.h"

namespace net {

QuicBlockedFrame::QuicBlockedFrame() {}

QuicBlockedFrame::QuicBlockedFrame(QuicControlFrameId control_frame_id,
                                   QuicStreamId stream_id)
    : QuicControlFrame(control_frame_id), stream_id(stream_id) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicBlockedFrame& blocked_frame) {
  os << "{ control_frame_id: " << blocked_frame.control_frame_id
     << ", stream_id: " << blocked_frame.stream_id << " }\n";
  return os;
}

}  // namespace net
