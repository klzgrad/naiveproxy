// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_RESET_STREAM_AT_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_RESET_STREAM_AT_FRAME_H_

#include <cstdint>
#include <ostream>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// RESET_STREAM_AT allows a QUIC application to reset a stream, but only after
// the receiver consumes data up to a certain point. Defined in
// <https://datatracker.ietf.org/doc/draft-ietf-quic-reliable-stream-reset/>.
struct QUICHE_EXPORT QuicResetStreamAtFrame {
  QuicResetStreamAtFrame() = default;
  QuicResetStreamAtFrame(QuicControlFrameId control_frame_id,
                         QuicStreamId stream_id, uint64_t error,
                         QuicStreamOffset final_offset,
                         QuicStreamOffset reliable_offset);

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const QuicResetStreamAtFrame& frame);

  bool operator==(const QuicResetStreamAtFrame& rhs) const;
  bool operator!=(const QuicResetStreamAtFrame& rhs) const;

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;

  QuicStreamId stream_id = 0;
  uint64_t error = 0;

  // The total number of bytes ever sent on the stream; used for flow control.
  QuicStreamOffset final_offset = 0;
  // The RESET_STREAM is active only after the application reads up to
  // `reliable_offset` bytes.
  QuicStreamOffset reliable_offset = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_RESET_STREAM_AT_FRAME_H_
