// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_MAX_STREAM_ID_FRAME_H_
#define NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_MAX_STREAM_ID_FRAME_H_

#include <ostream>

#include "net/third_party/quic/core/frames/quic_control_frame.h"

namespace quic {

// IETF format MAX_STREAM_ID frame.
// This frame is used by the sender to inform the peer of the largest
// stream id that the peer may open and that the sender will accept.
struct QUIC_EXPORT_PRIVATE QuicMaxStreamIdFrame : public QuicControlFrame {
  QuicMaxStreamIdFrame();
  QuicMaxStreamIdFrame(QuicControlFrameId control_frame_id,
                       QuicStreamId max_stream_id);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicMaxStreamIdFrame& frame);

  // The maximum stream id to support.
  QuicStreamId max_stream_id;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_MAX_STREAM_ID_FRAME_H_
