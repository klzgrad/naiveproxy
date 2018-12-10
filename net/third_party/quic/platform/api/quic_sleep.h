// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SLEEP_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SLEEP_H_

#include "net/third_party/quic/core/quic_time.h"
#include "net/third_party/quic/platform/impl/quic_sleep_impl.h"

namespace quic {

inline void QuicSleep(QuicTime::Delta duration) {
  QuicSleepImpl(duration);
}

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SLEEP_H_
