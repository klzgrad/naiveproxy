// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/quartc/quartc_connection_helper.h"

namespace quic {

QuartcConnectionHelper::QuartcConnectionHelper(const QuicClock* clock)
    : clock_(clock) {}

const QuicClock* QuartcConnectionHelper::GetClock() const {
  return clock_;
}

QuicRandom* QuartcConnectionHelper::GetRandomGenerator() {
  return QuicRandom::GetInstance();
}

QuicBufferAllocator* QuartcConnectionHelper::GetStreamSendBufferAllocator() {
  return &buffer_allocator_;
}

}  // namespace quic
