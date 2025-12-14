// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_retire_connection_id_frame.h"

#include <ostream>

namespace quic {

QuicRetireConnectionIdFrame::QuicRetireConnectionIdFrame(
    QuicControlFrameId control_frame_id,
    QuicConnectionIdSequenceNumber sequence_number)
    : control_frame_id(control_frame_id), sequence_number(sequence_number) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicRetireConnectionIdFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", sequence_number: " << frame.sequence_number << " }\n";
  return os;
}

}  // namespace quic
