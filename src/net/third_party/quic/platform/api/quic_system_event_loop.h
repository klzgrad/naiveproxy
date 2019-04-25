// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SYSTEM_EVENT_LOOP_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SYSTEM_EVENT_LOOP_H_

#include "net/third_party/quic/platform/impl/quic_system_event_loop_impl.h"

inline void QuicRunSystemEventLoopIteration() {
  QuicRunSystemEventLoopIterationImpl();
}

using QuicSystemEventLoop = QuicSystemEventLoopImpl;

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SYSTEM_EVENT_LOOP_H_
