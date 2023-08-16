// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_ACK_FREQUENCY_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_ACK_FREQUENCY_FRAME_H_

#include <cstdint>
#include <ostream>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A frame that allows sender control of acknowledgement delays.
struct QUIC_EXPORT_PRIVATE QuicAckFrequencyFrame {
  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os, const QuicAckFrequencyFrame& ack_frequency_frame);

  QuicAckFrequencyFrame() = default;
  QuicAckFrequencyFrame(QuicControlFrameId control_frame_id,
                        uint64_t sequence_number, uint64_t packet_tolerance,
                        QuicTime::Delta max_ack_delay);

  // A unique identifier of this control frame. 0 when this frame is
  // received, and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;

  // If true, do not ack immediately upon observeation of packet reordering.
  bool ignore_order = false;

  // Sequence number assigned to the ACK_FREQUENCY frame by the sender to allow
  // receivers to ignore obsolete frames.
  uint64_t sequence_number = 0;

  // The maximum number of ack-eliciting packets after which the receiver sends
  // an acknowledgement. Invald if == 0.
  uint64_t packet_tolerance = 2;

  // The maximum time that ack packets can be delayed.
  QuicTime::Delta max_ack_delay =
      QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_ACK_FREQUENCY_FRAME_H_
