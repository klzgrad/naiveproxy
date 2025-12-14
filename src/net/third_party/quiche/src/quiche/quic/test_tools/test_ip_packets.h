// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_IP_PACKET_GENERATION_H_
#define QUICHE_QUIC_TEST_TOOLS_IP_PACKET_GENERATION_H_

#include <string>

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/quiche_ip_address.h"

namespace quic::test {

enum class IpPacketPayloadType {
  kUdp,
};

// Create an IP packet, appropriate for sending to a raw IP socket.
std::string CreateIpPacket(
    const quiche::QuicheIpAddress& source_address,
    const quiche::QuicheIpAddress& destination_address,
    absl::string_view payload,
    IpPacketPayloadType payload_type = IpPacketPayloadType::kUdp);

// Create a UDP packet, appropriate for sending to a raw UDP socket or including
// as the payload of an IP packet.
std::string CreateUdpPacket(const QuicSocketAddress& source_address,
                            const QuicSocketAddress& destination_address,
                            absl::string_view payload);

}  // namespace quic::test

#endif  // QUICHE_QUIC_TEST_TOOLS_IP_PACKET_GENERATION_H_
