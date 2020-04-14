// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_SLEEP_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_SLEEP_H_

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/quic/platform/impl/quic_sleep_impl.h"

namespace quic {

inline void QuicSleep(QuicTime::Delta duration) {
  QuicSleepImpl(duration);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_SLEEP_H_
