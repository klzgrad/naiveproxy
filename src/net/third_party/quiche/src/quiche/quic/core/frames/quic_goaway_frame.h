// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_GOAWAY_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_GOAWAY_FRAME_H_

#include <ostream>
#include <string>

#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"

namespace quic {

struct QUICHE_EXPORT QuicGoAwayFrame {
  QuicGoAwayFrame() = default;
  QuicGoAwayFrame(QuicControlFrameId control_frame_id, QuicErrorCode error_code,
                  QuicStreamId last_good_stream_id, const std::string& reason);

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const QuicGoAwayFrame& g);

  bool operator==(const QuicGoAwayFrame& rhs) const;
  bool operator!=(const QuicGoAwayFrame& rhs) const;

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;
  QuicErrorCode error_code = QUIC_NO_ERROR;
  QuicStreamId last_good_stream_id = 0;
  std::string reason_phrase;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_GOAWAY_FRAME_H_
