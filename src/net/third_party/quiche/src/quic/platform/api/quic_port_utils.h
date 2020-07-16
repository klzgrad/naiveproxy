// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_PORT_UTILS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_PORT_UTILS_H_

#include "net/quic/platform/impl/quic_port_utils_impl.h"

namespace quic {

// Returns a UDP port that is currently unused.  Check-fails if none are
// available. May return 0 in which case the bind() call will cause the OS
// to use an unused port.
inline int QuicPickServerPortForTestsOrDie() {
  return QuicPickServerPortForTestsOrDieImpl();
}

// Indicates that a specified port previously returned by
// QuicPickServerPortForTestsOrDie is no longer used.
inline void QuicRecyclePort(int port) {
  return QuicRecyclePortImpl(port);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_PORT_UTILS_H_
