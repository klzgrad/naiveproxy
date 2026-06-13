// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EVENT_LOOP_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EVENT_LOOP_IMPL_H_

#include <vector>

namespace quic {
class QuicEventLoopFactory;
}

namespace quiche {

inline quic::QuicEventLoopFactory* GetOverrideForDefaultEventLoopImpl() {
  return nullptr;
}

inline std::vector<quic::QuicEventLoopFactory*>
GetExtraEventLoopImplementationsImpl() {
  return {};
}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EVENT_LOOP_IMPL_H_
