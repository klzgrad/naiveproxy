// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_handshake_done_frame.h"

namespace quic {

QuicHandshakeDoneFrame::QuicHandshakeDoneFrame()
    : QuicInlinedFrame(HANDSHAKE_DONE_FRAME),
      control_frame_id(kInvalidControlFrameId) {}

QuicHandshakeDoneFrame::QuicHandshakeDoneFrame(
    QuicControlFrameId control_frame_id)
    : QuicInlinedFrame(HANDSHAKE_DONE_FRAME),
      control_frame_id(control_frame_id) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicHandshakeDoneFrame& handshake_done_frame) {
  os << "{ control_frame_id: " << handshake_done_frame.control_frame_id
     << " }\n";
  return os;
}

}  // namespace quic
