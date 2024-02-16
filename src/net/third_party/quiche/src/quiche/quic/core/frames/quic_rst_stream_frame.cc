// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_rst_stream_frame.h"

#include "quiche/quic/core/quic_error_codes.h"

namespace quic {

QuicRstStreamFrame::QuicRstStreamFrame(QuicControlFrameId control_frame_id,
                                       QuicStreamId stream_id,
                                       QuicRstStreamErrorCode error_code,
                                       QuicStreamOffset bytes_written)
    : control_frame_id(control_frame_id),
      stream_id(stream_id),
      error_code(error_code),
      ietf_error_code(RstStreamErrorCodeToIetfResetStreamErrorCode(error_code)),
      byte_offset(bytes_written) {}

QuicRstStreamFrame::QuicRstStreamFrame(QuicControlFrameId control_frame_id,
                                       QuicStreamId stream_id,
                                       QuicResetStreamError error,
                                       QuicStreamOffset bytes_written)
    : control_frame_id(control_frame_id),
      stream_id(stream_id),
      error_code(error.internal_code()),
      ietf_error_code(error.ietf_application_code()),
      byte_offset(bytes_written) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicRstStreamFrame& rst_frame) {
  os << "{ control_frame_id: " << rst_frame.control_frame_id
     << ", stream_id: " << rst_frame.stream_id
     << ", byte_offset: " << rst_frame.byte_offset
     << ", error_code: " << rst_frame.error_code
     << ", ietf_error_code: " << rst_frame.ietf_error_code << " }\n";
  return os;
}

bool QuicRstStreamFrame::operator==(const QuicRstStreamFrame& rhs) const {
  return control_frame_id == rhs.control_frame_id &&
         stream_id == rhs.stream_id && byte_offset == rhs.byte_offset &&
         error_code == rhs.error_code && ietf_error_code == rhs.ietf_error_code;
}

bool QuicRstStreamFrame::operator!=(const QuicRstStreamFrame& rhs) const {
  return !(*this == rhs);
}

}  // namespace quic
