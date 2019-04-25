// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_PORT_UTILS_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_PORT_UTILS_H_

#include "net/third_party/quic/platform/impl/quic_port_utils_impl.h"

namespace quic {

// Returns a UDP port that is currently unused.  Check-fails if none are
// available.
inline int QuicPickUnusedPortOrDie() {
  return QuicPickUnusedPortOrDieImpl();
}

// Indicates that a specified port previously returned by
// QuicPickUnusedPortOrDie is no longer used.
inline void QuicRecyclePort(int port) {
  return QuicRecyclePortImpl(port);
}

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_PORT_UTILS_H_
