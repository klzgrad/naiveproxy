// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_PATH_CHALLENGE_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_PATH_CHALLENGE_FRAME_H_

#include <memory>
#include <ostream>

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicPathChallengeFrame
    : public QuicInlinedFrame<QuicPathChallengeFrame> {
  QuicPathChallengeFrame();
  QuicPathChallengeFrame(QuicControlFrameId control_frame_id,
                         const QuicPathFrameBuffer& data_buff);
  ~QuicPathChallengeFrame() = default;

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os, const QuicPathChallengeFrame& frame);

  QuicFrameType type;

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;

  QuicPathFrameBuffer data_buffer{};
};
}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_PATH_CHALLENGE_FRAME_H_
