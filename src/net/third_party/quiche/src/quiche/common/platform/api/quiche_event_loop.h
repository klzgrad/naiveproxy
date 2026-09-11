// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUIC_EVENT_LOOP_H_
#define QUICHE_COMMON_PLATFORM_API_QUIC_EVENT_LOOP_H_

#include "quiche_platform_impl/quiche_event_loop_impl.h"

namespace quic {
class QuicEventLoopFactory;
}

namespace quiche {

inline quic::QuicEventLoopFactory* GetOverrideForDefaultEventLoop() {
  return GetOverrideForDefaultEventLoopImpl();
}

inline std::vector<quic::QuicEventLoopFactory*>
GetExtraEventLoopImplementations() {
  return GetExtraEventLoopImplementationsImpl();
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUIC_EVENT_LOOP_H_
