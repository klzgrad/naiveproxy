// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_CONTROL_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_CONTROL_FRAME_H_

#include "net/quic/core/quic_constants.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

struct QUIC_EXPORT_PRIVATE QuicControlFrame {
  QuicControlFrame() : control_frame_id(kInvalidControlFrameId) {}
  explicit QuicControlFrame(QuicControlFrameId control_frame_id)
      : control_frame_id(control_frame_id) {}

  // A unique identifier of this control frame. 0 when this frame is received,
  // and non-zero when sent.
  QuicControlFrameId control_frame_id;
};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_CONTROL_FRAME_H_
