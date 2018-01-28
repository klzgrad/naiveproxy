// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_FRAMES_QUIC_PING_FRAME_H_
#define NET_QUIC_CORE_FRAMES_QUIC_PING_FRAME_H_

#include "net/quic/platform/api/quic_export.h"

namespace net {

// A ping frame contains no payload, though it is retransmittable,
// and ACK'd just like other normal frames.
struct QUIC_EXPORT_PRIVATE QuicPingFrame {};

}  // namespace net

#endif  // NET_QUIC_CORE_FRAMES_QUIC_PING_FRAME_H_
