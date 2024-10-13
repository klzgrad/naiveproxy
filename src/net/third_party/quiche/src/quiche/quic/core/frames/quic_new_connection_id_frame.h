// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_NEW_CONNECTION_ID_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_NEW_CONNECTION_ID_FRAME_H_

#include <ostream>

#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

struct QUICHE_EXPORT QuicNewConnectionIdFrame {
  QuicNewConnectionIdFrame() = default;
  QuicNewConnectionIdFrame(QuicControlFrameId control_frame_id,
                           QuicConnectionId connection_id,
                           QuicConnectionIdSequenceNumber sequence_number,
                           StatelessResetToken stateless_reset_token,
                           uint64_t retire_prior_to);

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicNewConnectionIdFrame& frame);

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;
  QuicConnectionId connection_id = EmptyQuicConnectionId();
  QuicConnectionIdSequenceNumber sequence_number = 0;
  StatelessResetToken stateless_reset_token;
  uint64_t retire_prior_to = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_NEW_CONNECTION_ID_FRAME_H_
