// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_window_update_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"

namespace quic {

QuicWindowUpdateFrame::QuicWindowUpdateFrame()
    : control_frame_id(kInvalidControlFrameId) {}

QuicWindowUpdateFrame::QuicWindowUpdateFrame(
    QuicControlFrameId control_frame_id,
    QuicStreamId stream_id,
    QuicByteCount max_data)
    : control_frame_id(control_frame_id),
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
