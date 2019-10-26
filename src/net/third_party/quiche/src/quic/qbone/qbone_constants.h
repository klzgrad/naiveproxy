// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_CONSTANTS_H_
#define QUICHE_QUIC_QBONE_QBONE_CONSTANTS_H_

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/qbone/platform/ip_range.h"

namespace quic {

struct QboneConstants {
  // Qbone's ALPN
  static constexpr char kQboneAlpn[] = "qbone";
  // The maximum number of bytes allowed in a qbone packet.
  static const QuicByteCount kMaxQbonePacketBytes = 2000;
  // The table id for Qbone's routing table. 'bone' in ascii.
  static const uint32_t kQboneRouteTableId = 0x626F6E65;
  // The stream ID of the control channel.
  static QuicStreamId GetControlStreamId(QuicTransportVersion version);
  // The link-local address of the Terminator
  static const QuicIpAddress* TerminatorLocalAddress();
  // The IPRange containing the TerminatorLocalAddress
  static const IpRange* TerminatorLocalAddressRange();
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_CONSTANTS_H_
