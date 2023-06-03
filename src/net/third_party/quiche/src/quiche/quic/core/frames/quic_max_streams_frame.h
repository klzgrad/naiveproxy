// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_MAX_STREAMS_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_MAX_STREAMS_FRAME_H_

#include <ostream>

#include "quiche/quic/core/frames/quic_inlined_frame.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// IETF format MAX_STREAMS frame.
// This frame is used by the sender to inform the peer of the number of
// streams that the peer may open and that the sender will accept.
struct QUIC_EXPORT_PRIVATE QuicMaxStreamsFrame
    : public QuicInlinedFrame<QuicMaxStreamsFrame> {
  QuicMaxStreamsFrame();
  QuicMaxStreamsFrame(QuicControlFrameId control_frame_id,
                      QuicStreamCount stream_count, bool unidirectional);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os, const QuicMaxStreamsFrame& frame);

  QuicFrameType type;

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id = kInvalidControlFrameId;

  // The number of streams that may be opened.
  QuicStreamCount stream_count = 0;
  // Whether uni- or bi-directional streams
  bool unidirectional = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_MAX_STREAMS_FRAME_H_
