// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"

#include <cstdint>
#include <ostream>

namespace quic {

QuicAckFrequencyFrame::QuicAckFrequencyFrame(
    QuicControlFrameId control_frame_id, uint64_t sequence_number,
    uint64_t ack_eliciting_threshold, QuicTime::Delta requested_max_ack_delay,
    uint64_t reordering_threshold)
    : control_frame_id(control_frame_id),
      sequence_number(sequence_number),
      ack_eliciting_threshold(ack_eliciting_threshold),
      requested_max_ack_delay(requested_max_ack_delay),
      reordering_threshold(reordering_threshold) {}

std::ostream& operator<<(std::ostream& os, const QuicAckFrequencyFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", sequence_number: " << frame.sequence_number
     << ", ack_eliciting_threshold: " << frame.ack_eliciting_threshold
     << ", requested_max_ack_delay_ms: "
     << frame.requested_max_ack_delay.ToMilliseconds()
     << ", reordering_threshold: " << frame.reordering_threshold << " }\n";
  return os;
}

}  // namespace quic
