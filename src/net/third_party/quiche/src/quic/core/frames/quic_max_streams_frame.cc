// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_max_streams_frame.h"

namespace quic {

QuicMaxStreamsFrame::QuicMaxStreamsFrame()
    : QuicInlinedFrame(MAX_STREAMS_FRAME),
      control_frame_id(kInvalidControlFrameId),
      stream_count(0),
      unidirectional(false) {}

QuicMaxStreamsFrame::QuicMaxStreamsFrame(QuicControlFrameId control_frame_id,
                                         QuicStreamCount stream_count,
                                         bool unidirectional)
    : QuicInlinedFrame(MAX_STREAMS_FRAME),
      control_frame_id(control_frame_id),
      stream_count(stream_count),
      unidirectional(unidirectional) {}

std::ostream& operator<<(std::ostream& os, const QuicMaxStreamsFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", stream_count: " << frame.stream_count
     << ((frame.unidirectional) ? ", unidirectional }\n"
                                : ", bidirectional }\n");
  return os;
}

}  // namespace quic
