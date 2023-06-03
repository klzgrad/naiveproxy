// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_window_update_frame.h"

#include "quiche/quic/core/quic_types.h"

namespace quic {

QuicWindowUpdateFrame::QuicWindowUpdateFrame()
    : QuicInlinedFrame(WINDOW_UPDATE_FRAME) {}

QuicWindowUpdateFrame::QuicWindowUpdateFrame(
    QuicControlFrameId control_frame_id, QuicStreamId stream_id,
    QuicByteCount max_data)
    : QuicInlinedFrame(WINDOW_UPDATE_FRAME),
      control_frame_id(control_frame_id),
      stream_id(stream_id),
      max_data(max_data) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicWindowUpdateFrame& window_update_frame) {
  os << "{ control_frame_id: " << window_update_frame.control_frame_id
     << ", stream_id: " << window_update_frame.stream_id
     << ", max_data: " << window_update_frame.max_data << " }\n";
  return os;
}

}  // namespace quic
