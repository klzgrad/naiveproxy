// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_RETIRE_CONNECTION_ID_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_RETIRE_CONNECTION_ID_FRAME_H_

#include <ostream>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

struct QUICHE_EXPORT QuicRetireConnectionIdFrame {
  QuicRetireConnectionIdFrame() = default;
  QuicRetireConnectionIdFrame(QuicControlFrameId control_frame_id,
                              QuicConnectionIdSequenceNumber sequence_number);

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicRetireConnectionIdFrame& frame);

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;
  QuicConnectionIdSequenceNumber sequence_number = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_RETIRE_CONNECTION_ID_FRAME_H_
