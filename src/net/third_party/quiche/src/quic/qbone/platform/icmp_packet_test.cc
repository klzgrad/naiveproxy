// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/icmp_packet.h"

#include <netinet/ip6.h>

#include <cstdint>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {
namespace {

constexpr char kReferenceSourceAddress[] = "fe80:1:2:3:4::1";
constexpr char kReferenceDestinationAddress[] = "fe80:4:3:2:1::1";

// clang-format off
constexpr  uint8_t kReferenceICMPMessageBody[] {
    0xd2, 0x61, 0x29, 0x5b, 0x00, 0x00, 0x00, 0x00,
    0x0d, 0x59, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37
};

constexpr uint8_t kReferenceICMPPacket[] = {
    // START IPv6 Header
    // IPv6 with zero TOS and flow label.
    0x60, 0x00, 0x00, 0x00,
    // Payload is 64 bytes
    0x00, 0x40,
    // Next header is 58
    0x3a,
    // Hop limit is 64
    0x40,
    // Source address of fe80:1:2:3:4::1
    0xfe, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00, 0x03,
    0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // Destination address of fe80:4:3:2:1::1
    0xfe, 0x80, 0x00, 0x04, 0x00, 0x03, 0x00, 0x02,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    // END IPv6 Header
    // START ICMPv6 Header
    // Echo Request, zero code
    0x80, 0x00,
    // Checksum
    0xec, 0x00,
    // Identifier
    0xcb, 0x82,
    // Sequence Number
    0x00, 0x01,
    // END ICMPv6 Header
    // Message body
    0xd2, 0x61, 0x29, 0x5b, 0x00, 0x00, 0x00, 0x00,
    0x0d, 0x59, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37
};
// clang-format on

}  // namespace

TEST(IcmpPacketTest, CreatedPacketMatchesReference) {
  QuicIpAddress src;
  ASSERT_TRUE(src.FromString(kReferenceSourceAddress));
  in6_addr src_addr;
  memcpy(src_addr.s6_addr, src.ToPackedString().data(), sizeof(in6_addr));

  QuicIpAddress dst;
  ASSERT_TRUE(dst.FromString(kReferenceDestinationAddress));
  in6_addr dst_addr;
  memcpy(dst_addr.s6_addr, dst.ToPackedString().data(), sizeof(in6_addr));

  icmp6_hdr icmp_header{};
  icmp_header.icmp6_type = ICMP6_ECHO_REQUEST;
  icmp_header.icmp6_id = 0x82cb;
  icmp_header.icmp6_seq = 0x0100;

  QuicStringPiece message_body = QuicStringPiece(
      reinterpret_cast<const char*>(kReferenceICMPMessageBody), 56);
  QuicStringPiece expected_packet =
      QuicStringPiece(reinterpret_cast<const char*>(kReferenceICMPPacket), 104);
  CreateIcmpPacket(src_addr, dst_addr, icmp_header, message_body,
                   [&expected_packet](QuicStringPiece packet) {
                     QUIC_LOG(INFO) << QuicTextUtils::HexDump(packet);
                     ASSERT_EQ(packet, expected_packet);
                   });
}

TEST(IcmpPacketTest, NonZeroChecksumIsIgnored) {
  QuicIpAddress src;
  ASSERT_TRUE(src.FromString(kReferenceSourceAddress));
  in6_addr src_addr;
  memcpy(src_addr.s6_addr, src.ToPackedString().data(), sizeof(in6_addr));

  QuicIpAddress dst;
  ASSERT_TRUE(dst.FromString(kReferenceDestinationAddress));
  in6_addr dst_addr;
  memcpy(dst_addr.s6_addr, dst.ToPackedString().data(), sizeof(in6_addr));

  icmp6_hdr icmp_header{};
  icmp_header.icmp6_type = ICMP6_ECHO_REQUEST;
  icmp_header.icmp6_id = 0x82cb;
  icmp_header.icmp6_seq = 0x0100;
  // Set the checksum to a bogus value
  icmp_header.icmp6_cksum = 0x1234;

  QuicStringPiece message_body = QuicStringPiece(
      reinterpret_cast<const char*>(kReferenceICMPMessageBody), 56);
  QuicStringPiece expected_packet =
      QuicStringPiece(reinterpret_cast<const char*>(kReferenceICMPPacket), 104);
  CreateIcmpPacket(src_addr, dst_addr, icmp_header, message_body,
                   [&expected_packet](QuicStringPiece packet) {
                     QUIC_LOG(INFO) << QuicTextUtils::HexDump(packet);
                     ASSERT_EQ(packet, expected_packet);
                   });
}

}  // namespace quic
