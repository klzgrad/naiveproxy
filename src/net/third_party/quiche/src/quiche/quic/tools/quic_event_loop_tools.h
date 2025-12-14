// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_EVENT_LOOP_TOOLS_H_
#define QUICHE_QUIC_TOOLS_QUIC_EVENT_LOOP_TOOLS_H_

#include "absl/base/attributes.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {

inline constexpr QuicTimeDelta kDefaultTimeoutForTools =
    QuicTimeDelta::FromSeconds(3);
inline constexpr QuicTimeDelta kDefaultEventLoopTimeoutForTools =
    QuicTimeDelta::FromMilliseconds(50);

// Runs the event loop until the specified callback returns true, or until the
// timeout occurs. Returns true if callback returned true at least once.
ABSL_MUST_USE_RESULT inline bool ProcessEventsUntil(
    QuicEventLoop* event_loop, quiche::UnretainedCallback<bool()> callback,
    QuicTimeDelta timeout = kDefaultTimeoutForTools) {
  const QuicClock* clock = event_loop->GetClock();
  QuicTime start = clock->Now();
  while (!callback()) {
    event_loop->RunEventLoopOnce(kDefaultEventLoopTimeoutForTools);
    QuicTimeDelta elapsed = clock->Now() - start;
    if (elapsed >= timeout) {
      return false;
    }
  }
  return true;
}

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_EVENT_LOOP_TOOLS_H_
