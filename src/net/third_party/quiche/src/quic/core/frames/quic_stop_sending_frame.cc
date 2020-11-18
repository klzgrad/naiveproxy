// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_stop_sending_frame.h"

namespace quic {

QuicStopSendingFrame::QuicStopSendingFrame(QuicControlFrameId control_frame_id,
                                           QuicStreamId stream_id,
                                           QuicRstStreamErrorCode error_code)
    : control_frame_id(control_frame_id),
      stream_id(stream_id),
      error_code(error_code),
      ietf_error_code(
          GetQuicReloadableFlag(quic_stop_sending_uses_ietf_error_code)
              ? RstStreamErrorCodeToIetfResetStreamErrorCode(error_code)
              : static_cast<uint64_t>(error_code)) {
  if (GetQuicReloadableFlag(quic_stop_sending_uses_ietf_error_code)) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_stop_sending_uses_ietf_error_code, 1, 2);
  }
}

std::ostream& operator<<(std::ostream& os, const QuicStopSendingFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", stream_id: " << frame.stream_id
     << ", error_code: " << frame.error_code
     << ", ietf_error_code: " << frame.ietf_error_code << " }\n";
  return os;
}

}  // namespace quic
