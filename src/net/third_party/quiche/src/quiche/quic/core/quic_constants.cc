// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_constants.h"

#include <algorithm>
#include <cstdint>

#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

const char* const kFinalOffsetHeaderKey = ":final-offset";

const char* const kEPIDGoogleFrontEnd = "GFE";
const char* const kEPIDGoogleFrontEnd0 = "GFE0";

QuicPacketNumber MaxRandomInitialPacketNumber() {
  static const QuicPacketNumber kMaxRandomInitialPacketNumber =
      QuicPacketNumber(0x7fffffff);
  return kMaxRandomInitialPacketNumber;
}

QuicPacketNumber FirstSendingPacketNumber() {
  static const QuicPacketNumber kFirstSendingPacketNumber = QuicPacketNumber(1);
  return kFirstSendingPacketNumber;
}

int64_t GetDefaultDelayedAckTimeMs() {
  // The delayed ack time must not be greater than half the min RTO.
  return std::min<int64_t>(GetQuicFlag(quic_default_delayed_ack_time_ms),
                           kMinRetransmissionTimeMs / 2);
}

}  // namespace quic
