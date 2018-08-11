// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_IETF_BLOCKED_FRAME_H_
#define NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_IETF_BLOCKED_FRAME_H_

#include <ostream>

#include "net/third_party/quic/core/frames/quic_control_frame.h"

namespace net {

// IETF format BLOCKED frame.
// The sender uses the BLOCKED frame to inform the receiver that the
// sender is unable to send data because of connection-level flow control.
struct QUIC_EXPORT_PRIVATE QuicIetfBlockedFrame : public QuicControlFrame {
  QuicIetfBlockedFrame();
  QuicIetfBlockedFrame(QuicControlFrameId control_frame_id,
                       QuicStreamOffset offset);

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicIetfBlockedFrame& frame);

  // Offset at which the BLOCKED applies
  QuicStreamOffset offset;
};

}  // namespace net

#endif  // NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_IETF_BLOCKED_FRAME_H_
