// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/frames/quic_ietf_blocked_frame.h"

namespace net {

QuicIetfBlockedFrame::QuicIetfBlockedFrame() {}

QuicIetfBlockedFrame::QuicIetfBlockedFrame(QuicControlFrameId control_frame_id,
                                           QuicStreamOffset offset)
    : QuicControlFrame(control_frame_id), offset(offset) {}

std::ostream& operator<<(std::ostream& os, const QuicIetfBlockedFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", offset: " << frame.offset << " }\n";
  return os;
}

}  // namespace net
