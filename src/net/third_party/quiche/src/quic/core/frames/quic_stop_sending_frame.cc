// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/frames/quic_stop_sending_frame.h"

#include "quic/core/quic_error_codes.h"

namespace quic {

QuicStopSendingFrame::QuicStopSendingFrame(QuicControlFrameId control_frame_id,
                                           QuicStreamId stream_id,
                                           QuicRstStreamErrorCode error_code)
    : QuicStopSendingFrame(control_frame_id, stream_id,
                           QuicResetStreamError::FromInternal(error_code)) {}

QuicStopSendingFrame::QuicStopSendingFrame(QuicControlFrameId control_frame_id,
                                           QuicStreamId stream_id,
                                           QuicResetStreamError error)
    : control_frame_id(control_frame_id),
      stream_id(stream_id),
      error_code(error.internal_code()),
      ietf_error_code(error.ietf_application_code()) {}

std::ostream& operator<<(std::ostream& os, const QuicStopSendingFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", stream_id: " << frame.stream_id
     << ", error_code: " << frame.error_code
     << ", ietf_error_code: " << frame.ietf_error_code << " }\n";
  return os;
}

}  // namespace quic
