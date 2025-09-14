// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/frames/quic_immediate_ack_frame.h"

#include <ostream>

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

QuicImmediateAckFrame::QuicImmediateAckFrame()
    : QuicInlinedFrame(IMMEDIATE_ACK_FRAME) {}

std::ostream& operator<<(std::ostream& os,
                         const QuicImmediateAckFrame& /*immediate_ack_frame*/) {
  os << "{ }\n";
  return os;
}

}  // namespace quic
