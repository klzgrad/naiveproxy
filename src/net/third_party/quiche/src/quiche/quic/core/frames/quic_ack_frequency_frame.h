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
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// A frame that allows sender control of acknowledgement delays.
struct QUICHE_EXPORT QuicAckFrequencyFrame {
  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicAckFrequencyFrame& ack_frequency_frame);

  QuicAckFrequencyFrame() = default;
  // Set a default for reordering_threshold so it does not break Envoy tests.
  QuicAckFrequencyFrame(QuicControlFrameId control_frame_id,
                        uint64_t sequence_number,
                        uint64_t ack_eliciting_threshold,
                        QuicTime::Delta requested_max_ack_delay,
                        uint64_t reordering_threshold = 1);

  // A unique identifier of this control frame. 0 when this frame is
  // received, and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;

  // Sequence number assigned to the ACK_FREQUENCY frame by the sender to allow
  // receivers to ignore obsolete frames.
  uint64_t sequence_number = 0;

  // The maximum number of ack-eliciting packets that do not require an
  // acknowledgement.
  uint64_t ack_eliciting_threshold = 1;

  // The maximum time that ack packets can be delayed.
  QuicTime::Delta requested_max_ack_delay =
      QuicTime::Delta::FromMilliseconds(kDefaultPeerDelayedAckTimeMs);

  // The number of out-of-order packets necessary to trigger an immediate
  // acknowledgement. If zero, OOO packets are not acked immediately.
  uint64_t reordering_threshold = 1;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_ACK_FREQUENCY_FRAME_H_
