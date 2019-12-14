// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/tcp_packet.h"

#include <netinet/ip6.h>

#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {
namespace {

// clang-format off
constexpr uint8_t kReferenceTCPSYNPacket[] = {
  // START IPv6 Header
  // IPv6 with zero ToS and flow label
  0x60, 0x00, 0x00, 0x00,
  // Payload is 40 bytes
  0x00, 0x28,
  // Next header is TCP (6)
  0x06,
  // Hop limit is 64
  0x40,
  // Source address of ::1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
  // Destination address of ::1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
  // END IPv6 Header
  // START TCPv6 Header
  // Source port
  0xac, 0x1e,
  // Destination port
  0x27, 0x0f,
  // Sequence number
  0x4b, 0x01, 0xe8, 0x99,
  // Acknowledgement Sequence number,
  0x00, 0x00, 0x00, 0x00,
  // Offset
  0xa0,
  // Flags
  0x02,
  // Window
  0xaa, 0xaa,
  // Checksum
  0x2e, 0x21,
  // Urgent
  0x00, 0x00,
  // END TCPv6 Header
  // Options
  0x02, 0x04, 0xff, 0xc4, 0x04, 0x02, 0x08, 0x0a,
  0x1b, 0xb8, 0x52, 0xa1, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x03, 0x03, 0x07,
};

constexpr uint8_t kReferenceTCPRSTPacket[] = {
  // START IPv6 Header
  // IPv6 with zero ToS and flow label
  0x60, 0x00, 0x00, 0x00,
  // Payload is 20 bytes
  0x00, 0x14,
  // Next header is TCP (6)
  0x06,
  // Hop limit is 64
  0x40,
  // Source address of ::1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
  // Destination address of ::1
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
  // END IPv6 Header
  // START TCPv6 Header
  // Source port
  0x27, 0x0f,
  // Destination port
  0xac, 0x1e,
  // Sequence number
  0x00, 0x00, 0x00, 0x00,
  // Acknowledgement Sequence number,
  0x4b, 0x01, 0xe8, 0x9a,
  // Offset
  0x50,
  // Flags
  0x14,
  // Window
  0x00, 0x00,
  // Checksum
  0xa9, 0x05,
  // Urgent
  0x00, 0x00,
  // END TCPv6 Header
};
// clang-format on

}  // namespace

TEST(TcpPacketTest, CreatedPacketMatchesReference) {
  QuicStringPiece syn =
      QuicStringPiece(reinterpret_cast<const char*>(kReferenceTCPSYNPacket),
                      sizeof(kReferenceTCPSYNPacket));
  QuicStringPiece expected_packet =
      QuicStringPiece(reinterpret_cast<const char*>(kReferenceTCPRSTPacket),
                      sizeof(kReferenceTCPRSTPacket));
  CreateTcpResetPacket(syn, [&expected_packet](QuicStringPiece packet) {
    QUIC_LOG(INFO) << QuicTextUtils::HexDump(packet);
    ASSERT_EQ(packet, expected_packet);
  });
}

}  // namespace quic
