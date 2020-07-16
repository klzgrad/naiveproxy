// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_constants.h"

#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace quic {

constexpr char QboneConstants::kQboneAlpn[];
const QuicByteCount QboneConstants::kMaxQbonePacketBytes;
const uint32_t QboneConstants::kQboneRouteTableId;

QuicStreamId QboneConstants::GetControlStreamId(QuicTransportVersion version) {
  return QuicUtils::GetFirstBidirectionalStreamId(version,
                                                  Perspective::IS_CLIENT);
}

const QuicIpAddress* QboneConstants::TerminatorLocalAddress() {
  static auto* terminator_address = []() {
    QuicIpAddress* address = new QuicIpAddress;
    // 0x71 0x62 0x6f 0x6e 0x65 is 'qbone' in ascii.
    address->FromString("fe80::71:626f:6e65");
    return address;
  }();
  return terminator_address;
}

const IpRange* QboneConstants::TerminatorLocalAddressRange() {
  static auto* range =
      new quic::IpRange(*quic::QboneConstants::TerminatorLocalAddress(), 128);
  return range;
}

}  // namespace quic
