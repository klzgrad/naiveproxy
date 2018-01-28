// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/frames/quic_window_update_frame.h"

namespace net {

QuicWindowUpdateFrame::QuicWindowUpdateFrame(QuicStreamId stream_id,
                                             QuicStreamOffset byte_offset)
    : stream_id(stream_id), byte_offset(byte_offset) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicWindowUpdateFrame& window_update_frame) {
  os << "{ stream_id: " << window_update_frame.stream_id
     << ", byte_offset: " << window_update_frame.byte_offset << " }\n";
  return os;
}

}  // namespace net
