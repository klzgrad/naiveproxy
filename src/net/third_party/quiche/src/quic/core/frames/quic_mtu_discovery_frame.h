// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_MTU_DISCOVERY_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_MTU_DISCOVERY_FRAME_H_

#include "quic/core/frames/quic_inlined_frame.h"
#include "quic/core/quic_types.h"
#include "quic/platform/api/quic_export.h"

namespace quic {

// A path MTU discovery frame contains no payload and is serialized as a ping
// frame.
struct QUIC_EXPORT_PRIVATE QuicMtuDiscoveryFrame
    : public QuicInlinedFrame<QuicMtuDiscoveryFrame> {
  QuicMtuDiscoveryFrame() : QuicInlinedFrame(MTU_DISCOVERY_FRAME) {}

  QuicFrameType type;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_MTU_DISCOVERY_FRAME_H_
