// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/qbone_constants.h"

#include "quiche/quic/core/quic_utils.h"

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
    auto* address = new QuicIpAddress;
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

const QuicIpAddress* QboneConstants::GatewayAddress() {
  static auto* gateway_address = []() {
    auto* address = new QuicIpAddress;
    address->FromString("fe80::1");
    return address;
  }();
  return gateway_address;
}

}  // namespace quic
