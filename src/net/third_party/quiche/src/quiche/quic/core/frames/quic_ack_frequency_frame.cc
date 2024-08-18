// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"

#include <cstdint>
#include <limits>
#include <ostream>

namespace quic {

QuicAckFrequencyFrame::QuicAckFrequencyFrame(
    QuicControlFrameId control_frame_id, uint64_t sequence_number,
    uint64_t packet_tolerance, QuicTime::Delta max_ack_delay)
    : control_frame_id(control_frame_id),
      sequence_number(sequence_number),
      packet_tolerance(packet_tolerance),
      max_ack_delay(max_ack_delay) {}

std::ostream& operator<<(std::ostream& os, const QuicAckFrequencyFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", sequence_number: " << frame.sequence_number
     << ", packet_tolerance: " << frame.packet_tolerance
     << ", max_ack_delay_ms: " << frame.max_ack_delay.ToMilliseconds()
     << ", ignore_order: " << frame.ignore_order << " }\n";
  return os;
}

}  // namespace quic
