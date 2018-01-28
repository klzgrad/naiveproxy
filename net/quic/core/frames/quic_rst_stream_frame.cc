// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/frames/quic_rst_stream_frame.h"

namespace net {

QuicRstStreamFrame::QuicRstStreamFrame()
    : stream_id(0), error_code(QUIC_STREAM_NO_ERROR), byte_offset(0) {}

QuicRstStreamFrame::QuicRstStreamFrame(QuicStreamId stream_id,
                                       QuicRstStreamErrorCode error_code,
                                       QuicStreamOffset bytes_written)
    : stream_id(stream_id),
      error_code(error_code),
      byte_offset(bytes_written) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicRstStreamFrame& rst_frame) {
  os << "{ stream_id: " << rst_frame.stream_id
     << ", error_code: " << rst_frame.error_code << " }\n";
  return os;
}

}  // namespace net
