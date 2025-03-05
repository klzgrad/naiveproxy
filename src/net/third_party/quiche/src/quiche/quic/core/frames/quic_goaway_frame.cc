// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_goaway_frame.h"

#include <ostream>
#include <string>

namespace quic {

QuicGoAwayFrame::QuicGoAwayFrame(QuicControlFrameId control_frame_id,
                                 QuicErrorCode error_code,
                                 QuicStreamId last_good_stream_id,
                                 const std::string& reason)
    : control_frame_id(control_frame_id),
      error_code(error_code),
      last_good_stream_id(last_good_stream_id),
      reason_phrase(reason) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicGoAwayFrame& goaway_frame) {
  os << "{ control_frame_id: " << goaway_frame.control_frame_id
     << ", error_code: " << goaway_frame.error_code
     << ", last_good_stream_id: " << goaway_frame.last_good_stream_id
     << ", reason_phrase: '" << goaway_frame.reason_phrase << "' }\n";
  return os;
}

bool QuicGoAwayFrame::operator==(const QuicGoAwayFrame& rhs) const {
  return control_frame_id == rhs.control_frame_id &&
         error_code == rhs.error_code &&
         last_good_stream_id == rhs.last_good_stream_id &&
         reason_phrase == rhs.reason_phrase;
}

bool QuicGoAwayFrame::operator!=(const QuicGoAwayFrame& rhs) const {
  return !(*this == rhs);
}

}  // namespace quic
