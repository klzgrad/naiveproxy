// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/io/quic_default_event_loop.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "absl/algorithm/container.h"
#include "quiche/quic/core/io/quic_poll_event_loop.h"
#include "quiche/common/platform/api/quiche_event_loop.h"

#ifdef QUICHE_ENABLE_LIBEVENT
#include "quiche/quic/bindings/quic_libevent.h"
#endif

namespace quic {

QuicEventLoopFactory* GetDefaultEventLoop() {
  if (QuicEventLoopFactory* factory =
          quiche::GetOverrideForDefaultEventLoop()) {
    return factory;
  }
#ifdef QUICHE_ENABLE_LIBEVENT
  return QuicLibeventEventLoopFactory::Get();
#else
  return QuicPollEventLoopFactory::Get();
#endif
}

std::vector<QuicEventLoopFactory*> GetAllSupportedEventLoops() {
  std::vector<QuicEventLoopFactory*> loops = {QuicPollEventLoopFactory::Get()};
#ifdef QUICHE_ENABLE_LIBEVENT
  loops.push_back(QuicLibeventEventLoopFactory::Get());
  if (QuicLibeventEventLoopFactory::Get()->GetName() !=
      QuicLibeventEventLoopFactory::GetLevelTriggeredBackendForTests()
          ->GetName()) {
    loops.push_back(
        QuicLibeventEventLoopFactory::GetLevelTriggeredBackendForTests());
  }
#endif
  std::vector<QuicEventLoopFactory*> extra =
      quiche::GetExtraEventLoopImplementations();
  loops.insert(loops.end(), extra.begin(), extra.end());
  return loops;
}

}  // namespace quic
