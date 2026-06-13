// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_SYSTEM_EVENT_LOOP_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_SYSTEM_EVENT_LOOP_H_

#include "quiche_platform_impl/quiche_system_event_loop_impl.h"

namespace quiche {

inline void QuicheRunSystemEventLoopIteration() {
  QuicheRunSystemEventLoopIterationImpl();
}

using QuicheSystemEventLoop = QuicheSystemEventLoopImpl;

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_SYSTEM_EVENT_LOOP_H_
