// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_reset_stream_at_frame.h"

#include <cstdint>
#include <ostream>

#include "quiche/quic/core/quic_types.h"

namespace quic {

QuicResetStreamAtFrame::QuicResetStreamAtFrame(
    QuicControlFrameId control_frame_id, QuicStreamId stream_id, uint64_t error,
    QuicStreamOffset final_offset, QuicStreamOffset reliable_offset)
    : control_frame_id(control_frame_id),
      stream_id(stream_id),
      error(error),
      final_offset(final_offset),
      reliable_offset(reliable_offset) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicResetStreamAtFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", stream_id: " << frame.stream_id << ", error_code: " << frame.error
     << ", final_offset: " << frame.final_offset
     << ", reliable_offset: " << frame.reliable_offset << " }\n";
  return os;
}

bool QuicResetStreamAtFrame::operator==(
    const QuicResetStreamAtFrame& rhs) const {
  return control_frame_id == rhs.control_frame_id &&
         stream_id == rhs.stream_id && error == rhs.error &&
         final_offset == rhs.final_offset &&
         reliable_offset == rhs.reliable_offset;
}
bool QuicResetStreamAtFrame::operator!=(
    const QuicResetStreamAtFrame& rhs) const {
  return !(*this == rhs);
}

}  // namespace quic
