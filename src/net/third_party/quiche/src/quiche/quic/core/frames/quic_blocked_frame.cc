// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_blocked_frame.h"

#include <ostream>

#include "quiche/quic/core/quic_types.h"

namespace quic {

QuicBlockedFrame::QuicBlockedFrame() : QuicInlinedFrame(BLOCKED_FRAME) {}

QuicBlockedFrame::QuicBlockedFrame(QuicControlFrameId control_frame_id,
                                   QuicStreamId stream_id,
                                   QuicStreamOffset offset)
    : QuicInlinedFrame(BLOCKED_FRAME),
      control_frame_id(control_frame_id),
      stream_id(stream_id),
      offset(offset) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicBlockedFrame& blocked_frame) {
  os << "{ control_frame_id: " << blocked_frame.control_frame_id
     << ", stream_id: " << blocked_frame.stream_id
     << ", offset: " << blocked_frame.offset << " }\n";
  return os;
}

bool QuicBlockedFrame::operator==(const QuicBlockedFrame& rhs) const {
  return control_frame_id == rhs.control_frame_id &&
         stream_id == rhs.stream_id && offset == rhs.offset;
}

bool QuicBlockedFrame::operator!=(const QuicBlockedFrame& rhs) const {
  return !(*this == rhs);
}

}  // namespace quic
