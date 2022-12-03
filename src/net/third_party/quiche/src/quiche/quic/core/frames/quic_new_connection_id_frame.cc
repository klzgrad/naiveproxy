// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_new_connection_id_frame.h"

namespace quic {

QuicNewConnectionIdFrame::QuicNewConnectionIdFrame(
    QuicControlFrameId control_frame_id, QuicConnectionId connection_id,
    QuicConnectionIdSequenceNumber sequence_number,
    StatelessResetToken stateless_reset_token, uint64_t retire_prior_to)
    : control_frame_id(control_frame_id),
      connection_id(connection_id),
      sequence_number(sequence_number),
      stateless_reset_token(stateless_reset_token),
      retire_prior_to(retire_prior_to) {
  QUICHE_DCHECK(retire_prior_to <= sequence_number);
}

std::ostream& operator<<(std::ostream& os,
                         const QuicNewConnectionIdFrame& frame) {
  os << "{ control_frame_id: " << frame.control_frame_id
     << ", connection_id: " << frame.connection_id
     << ", sequence_number: " << frame.sequence_number
     << ", retire_prior_to: " << frame.retire_prior_to << " }\n";
  return os;
}

}  // namespace quic
