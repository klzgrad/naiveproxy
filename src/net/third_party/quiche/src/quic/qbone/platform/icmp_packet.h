// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_PLATFORM_ICMP_PACKET_H_
#define QUICHE_QUIC_QBONE_PLATFORM_ICMP_PACKET_H_

#include <netinet/icmp6.h>
#include <netinet/in.h>

#include <functional>

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// Creates an ICMPv6 packet, returning a packed string representation of the
// packet to |cb|. The resulting packet is given to a callback because it's
// stack allocated inside CreateIcmpPacket.
void CreateIcmpPacket(in6_addr src,
                      in6_addr dst,
                      const icmp6_hdr& icmp_header,
                      quiche::QuicheStringPiece body,
                      const std::function<void(quiche::QuicheStringPiece)>& cb);

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_PLATFORM_ICMP_PACKET_H_
