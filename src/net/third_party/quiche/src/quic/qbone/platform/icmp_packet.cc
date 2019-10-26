// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/icmp_packet.h"

#include <netinet/ip6.h>
#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/qbone/platform/internet_checksum.h"

namespace quic {
namespace {

constexpr size_t kIPv6AddressSize = sizeof(in6_addr);
constexpr size_t kIPv6HeaderSize = sizeof(ip6_hdr);
constexpr size_t kICMPv6HeaderSize = sizeof(icmp6_hdr);
constexpr size_t kIPv6MinPacketSize = 1280;
constexpr size_t kIcmpTtl = 64;
constexpr size_t kICMPv6BodyMaxSize =
    kIPv6MinPacketSize - kIPv6HeaderSize - kICMPv6HeaderSize;

struct ICMPv6Packet {
  ip6_hdr ip_header;
  icmp6_hdr icmp_header;
  uint8_t body[kICMPv6BodyMaxSize];
};

// pseudo header as described in RFC 2460 Section 8.1 (excluding addresses)
struct IPv6PseudoHeader {
  uint32_t payload_size{};
  uint8_t zeros[3] = {0, 0, 0};
  uint8_t next_header = IPPROTO_ICMPV6;
};

}  // namespace

void CreateIcmpPacket(in6_addr src,
                      in6_addr dst,
                      const icmp6_hdr& icmp_header,
                      QuicStringPiece body,
                      const std::function<void(QuicStringPiece)>& cb) {
  const size_t body_size = std::min(body.size(), kICMPv6BodyMaxSize);
  const size_t payload_size = kICMPv6HeaderSize + body_size;

  ICMPv6Packet icmp_packet{};
  // Set version to 6.
  icmp_packet.ip_header.ip6_vfc = 0x6 << 4;
  // Set the payload size, protocol and TTL.
  icmp_packet.ip_header.ip6_plen = QuicEndian::HostToNet16(payload_size);
  icmp_packet.ip_header.ip6_nxt = IPPROTO_ICMPV6;
  icmp_packet.ip_header.ip6_hops = kIcmpTtl;
  // Set the source address to the specified self IP.
  icmp_packet.ip_header.ip6_src = src;
  icmp_packet.ip_header.ip6_dst = dst;

  icmp_packet.icmp_header = icmp_header;
  // Per RFC 4443 Section 2.3, set checksum field to 0 prior to computing it
  icmp_packet.icmp_header.icmp6_cksum = 0;

  IPv6PseudoHeader pseudo_header{};
  pseudo_header.payload_size = QuicEndian::HostToNet32(payload_size);

  InternetChecksum checksum;
  // Pseudoheader.
  checksum.Update(icmp_packet.ip_header.ip6_src.s6_addr, kIPv6AddressSize);
  checksum.Update(icmp_packet.ip_header.ip6_dst.s6_addr, kIPv6AddressSize);
  checksum.Update(reinterpret_cast<char*>(&pseudo_header),
                  sizeof(pseudo_header));
  // ICMP header.
  checksum.Update(reinterpret_cast<const char*>(&icmp_packet.icmp_header),
                  sizeof(icmp_packet.icmp_header));
  // Body.
  checksum.Update(body.data(), body_size);
  icmp_packet.icmp_header.icmp6_cksum = checksum.Value();

  memcpy(icmp_packet.body, body.data(), body_size);

  const char* packet = reinterpret_cast<char*>(&icmp_packet);
  const size_t packet_size = offsetof(ICMPv6Packet, body) + body_size;

  cb(QuicStringPiece(packet, packet_size));
}

}  // namespace quic
