// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/tcp_packet.h"

#include <netinet/ip6.h>

#include "net/third_party/quiche/src/quic/platform/api/quic_endian.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/qbone/platform/internet_checksum.h"

namespace quic {
namespace {

constexpr size_t kIPv6AddressSize = sizeof(in6_addr);
constexpr size_t kTcpTtl = 64;

struct TCPv6Packet {
  ip6_hdr ip_header;
  tcphdr tcp_header;
};

struct TCPv6PseudoHeader {
  uint32_t payload_size{};
  uint8_t zeros[3] = {0, 0, 0};
  uint8_t next_header = IPPROTO_TCP;
};

}  // namespace

void CreateTcpResetPacket(
    quic::QuicStringPiece original_packet,
    const std::function<void(quic::QuicStringPiece)>& cb) {
  // By the time this method is called, original_packet should be fairly
  // strongly validated. However, it's better to be more paranoid than not, so
  // here are a bunch of very obvious checks.
  if (QUIC_PREDICT_FALSE(original_packet.size() < sizeof(ip6_hdr))) {
    return;
  }
  auto* ip6_header = reinterpret_cast<const ip6_hdr*>(original_packet.data());
  if (QUIC_PREDICT_FALSE(ip6_header->ip6_vfc >> 4 != 6)) {
    return;
  }
  if (QUIC_PREDICT_FALSE(ip6_header->ip6_nxt != IPPROTO_TCP)) {
    return;
  }
  if (QUIC_PREDICT_FALSE(QuicEndian::NetToHost16(ip6_header->ip6_plen) <
                         sizeof(tcphdr))) {
    return;
  }
  auto* tcp_header = reinterpret_cast<const tcphdr*>(ip6_header + 1);

  // Now that the original packet has been confirmed to be well-formed, it's
  // time to make the TCP RST packet.
  TCPv6Packet tcp_packet{};

  const size_t payload_size = sizeof(tcphdr);

  // Set version to 6.
  tcp_packet.ip_header.ip6_vfc = 0x6 << 4;
  // Set the payload size, protocol and TTL.
  tcp_packet.ip_header.ip6_plen = QuicEndian::HostToNet16(payload_size);
  tcp_packet.ip_header.ip6_nxt = IPPROTO_TCP;
  tcp_packet.ip_header.ip6_hops = kTcpTtl;
  // Since the TCP RST is impersonating the endpoint, flip the source and
  // destination addresses from the original packet.
  tcp_packet.ip_header.ip6_src = ip6_header->ip6_dst;
  tcp_packet.ip_header.ip6_dst = ip6_header->ip6_src;

  // The same is true about the TCP ports
  tcp_packet.tcp_header.dest = tcp_header->source;
  tcp_packet.tcp_header.source = tcp_header->dest;

  // There are no extensions in this header, so size is trivial
  tcp_packet.tcp_header.doff = sizeof(tcphdr) >> 2;
  // Checksum is 0 before it is computed
  tcp_packet.tcp_header.check = 0;

  // Per RFC 793, TCP RST comes in one of 3 flavors:
  //
  // * connection CLOSED
  // * connection in non-synchronized state (LISTEN, SYN-SENT, SYN-RECEIVED)
  // * connection in synchronized state (ESTABLISHED, FIN-WAIT-1, etc.)
  //
  // QBONE is acting like a firewall, so the RFC text of interest is the CLOSED
  // state. Note, however, that it is possible for a connection to actually be
  // in the FIN-WAIT-1 state on the remote end, but the processing logic does
  // not change.
  tcp_packet.tcp_header.rst = 1;

  // If the incoming segment has an ACK field, the reset takes its sequence
  // number from the ACK field of the segment,
  if (tcp_header->ack) {
    tcp_packet.tcp_header.seq = tcp_header->ack_seq;
  } else {
    // Otherwise the reset has sequence number zero and the ACK field is set to
    // the sum of the sequence number and segment length of the incoming segment
    tcp_packet.tcp_header.ack = 1;
    tcp_packet.tcp_header.seq = 0;
    tcp_packet.tcp_header.ack_seq =
        QuicEndian::HostToNet32(QuicEndian::NetToHost32(tcp_header->seq) + 1);
  }

  TCPv6PseudoHeader pseudo_header{};
  pseudo_header.payload_size = QuicEndian::HostToNet32(payload_size);

  InternetChecksum checksum;
  // Pseudoheader.
  checksum.Update(tcp_packet.ip_header.ip6_src.s6_addr, kIPv6AddressSize);
  checksum.Update(tcp_packet.ip_header.ip6_dst.s6_addr, kIPv6AddressSize);
  checksum.Update(reinterpret_cast<char*>(&pseudo_header),
                  sizeof(pseudo_header));
  // TCP header.
  checksum.Update(reinterpret_cast<const char*>(&tcp_packet.tcp_header),
                  sizeof(tcp_packet.tcp_header));
  // There is no body.
  tcp_packet.tcp_header.check = checksum.Value();

  const char* packet = reinterpret_cast<char*>(&tcp_packet);

  cb(QuicStringPiece(packet, sizeof(tcp_packet)));
}

}  // namespace quic
