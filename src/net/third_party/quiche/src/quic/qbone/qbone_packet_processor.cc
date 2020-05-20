// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/qbone_packet_processor.h"

#include <cstring>

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/qbone/platform/icmp_packet.h"
#include "net/third_party/quiche/src/quic/qbone/platform/internet_checksum.h"
#include "net/third_party/quiche/src/quic/qbone/platform/tcp_packet.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace {

constexpr size_t kIPv6AddressSize = 16;
constexpr size_t kIPv6MinPacketSize = 1280;
constexpr size_t kIcmpTtl = 64;
constexpr size_t kICMPv6DestinationUnreachableDueToSourcePolicy = 5;

}  // namespace

namespace quic {

const QuicIpAddress QbonePacketProcessor::kInvalidIpAddress =
    QuicIpAddress::Any6();

QbonePacketProcessor::QbonePacketProcessor(QuicIpAddress self_ip,
                                           QuicIpAddress client_ip,
                                           size_t client_ip_subnet_length,
                                           OutputInterface* output,
                                           StatsInterface* stats)
    : client_ip_(client_ip),
      output_(output),
      stats_(stats),
      filter_(new Filter) {
  memcpy(self_ip_.s6_addr, self_ip.ToPackedString().data(), kIPv6AddressSize);
  DCHECK_LE(client_ip_subnet_length, kIPv6AddressSize * 8);
  client_ip_subnet_length_ = client_ip_subnet_length;

  DCHECK(IpAddressFamily::IP_V6 == self_ip.address_family());
  DCHECK(IpAddressFamily::IP_V6 == client_ip.address_family());
  DCHECK(self_ip != kInvalidIpAddress);
}

QbonePacketProcessor::OutputInterface::~OutputInterface() {}
QbonePacketProcessor::StatsInterface::~StatsInterface() {}
QbonePacketProcessor::Filter::~Filter() {}

QbonePacketProcessor::ProcessingResult
QbonePacketProcessor::Filter::FilterPacket(
    Direction direction,
    quiche::QuicheStringPiece full_packet,
    quiche::QuicheStringPiece payload,
    icmp6_hdr* icmp_header,
    OutputInterface* output) {
  return ProcessingResult::OK;
}

void QbonePacketProcessor::ProcessPacket(std::string* packet,
                                         Direction direction) {
  if (QUIC_PREDICT_FALSE(!IsValid())) {
    QUIC_BUG << "QuicPacketProcessor is invoked in an invalid state.";
    stats_->OnPacketDroppedSilently(direction);
    return;
  }

  uint8_t transport_protocol;
  char* transport_data;
  icmp6_hdr icmp_header;
  memset(&icmp_header, 0, sizeof(icmp_header));
  ProcessingResult result = ProcessIPv6HeaderAndFilter(
      packet, direction, &transport_protocol, &transport_data, &icmp_header);

  switch (result) {
    case ProcessingResult::OK:
      switch (direction) {
        case Direction::FROM_OFF_NETWORK:
          output_->SendPacketToNetwork(*packet);
          break;
        case Direction::FROM_NETWORK:
          output_->SendPacketToClient(*packet);
          break;
      }
      stats_->OnPacketForwarded(direction);
      break;
    case ProcessingResult::SILENT_DROP:
      stats_->OnPacketDroppedSilently(direction);
      break;
    case ProcessingResult::DEFER:
      stats_->OnPacketDeferred(direction);
      break;
    case ProcessingResult::ICMP:
      SendIcmpResponse(&icmp_header, *packet, direction);
      stats_->OnPacketDroppedWithIcmp(direction);
      break;
    case ProcessingResult::ICMP_AND_TCP_RESET:
      SendIcmpResponse(&icmp_header, *packet, direction);
      stats_->OnPacketDroppedWithIcmp(direction);
      SendTcpReset(*packet, direction);
      stats_->OnPacketDroppedWithTcpReset(direction);
      break;
  }
}

QbonePacketProcessor::ProcessingResult
QbonePacketProcessor::ProcessIPv6HeaderAndFilter(std::string* packet,
                                                 Direction direction,
                                                 uint8_t* transport_protocol,
                                                 char** transport_data,
                                                 icmp6_hdr* icmp_header) {
  ProcessingResult result = ProcessIPv6Header(
      packet, direction, transport_protocol, transport_data, icmp_header);

  if (result == ProcessingResult::OK) {
    char* packet_data = &*packet->begin();
    size_t header_size = *transport_data - packet_data;
    // Sanity-check the bounds.
    if (packet_data >= *transport_data || header_size > packet->size() ||
        header_size < kIPv6HeaderSize) {
      QUIC_BUG << "Invalid pointers encountered in "
                  "QbonePacketProcessor::ProcessPacket.  Dropping the packet";
      return ProcessingResult::SILENT_DROP;
    }

    result = filter_->FilterPacket(
        direction, *packet,
        quiche::QuicheStringPiece(*transport_data,
                                  packet->size() - header_size),
        icmp_header, output_);
  }

  // Do not send ICMP error messages in response to ICMP errors.
  if (result == ProcessingResult::ICMP) {
    const uint8_t* header = reinterpret_cast<const uint8_t*>(packet->data());

    constexpr size_t kIPv6NextHeaderOffset = 6;
    constexpr size_t kIcmpMessageTypeOffset = kIPv6HeaderSize + 0;
    constexpr size_t kIcmpMessageTypeMaxError = 127;
    if (
        // Check size.
        packet->size() >= (kIPv6HeaderSize + kICMPv6HeaderSize) &&
        // Check that the packet is in fact ICMP.
        header[kIPv6NextHeaderOffset] == IPPROTO_ICMPV6 &&
        // Check that ICMP message type is an error.
        header[kIcmpMessageTypeOffset] < kIcmpMessageTypeMaxError) {
      result = ProcessingResult::SILENT_DROP;
    }
  }

  return result;
}

QbonePacketProcessor::ProcessingResult QbonePacketProcessor::ProcessIPv6Header(
    std::string* packet,
    Direction direction,
    uint8_t* transport_protocol,
    char** transport_data,
    icmp6_hdr* icmp_header) {
  // Check if the packet is big enough to have IPv6 header.
  if (packet->size() < kIPv6HeaderSize) {
    QUIC_DVLOG(1) << "Dropped malformed packet: IPv6 header too short";
    return ProcessingResult::SILENT_DROP;
  }

  // Check version field.
  ip6_hdr* header = reinterpret_cast<ip6_hdr*>(&*packet->begin());
  if (header->ip6_vfc >> 4 != 6) {
    QUIC_DVLOG(1) << "Dropped malformed packet: IP version is not IPv6";
    return ProcessingResult::SILENT_DROP;
  }

  // Check payload size.
  const size_t declared_payload_size =
      quiche::QuicheEndian::NetToHost16(header->ip6_plen);
  const size_t actual_payload_size = packet->size() - kIPv6HeaderSize;
  if (declared_payload_size != actual_payload_size) {
    QUIC_DVLOG(1)
        << "Dropped malformed packet: incorrect packet length specified";
    return ProcessingResult::SILENT_DROP;
  }

  // Check that the address of the client is in the packet.
  QuicIpAddress address_to_check;
  uint8_t address_reject_code;
  bool ip_parse_result;
  switch (direction) {
    case Direction::FROM_OFF_NETWORK:
      // Expect the source IP to match the client.
      ip_parse_result = address_to_check.FromPackedString(
          reinterpret_cast<const char*>(&header->ip6_src),
          sizeof(header->ip6_src));
      address_reject_code = kICMPv6DestinationUnreachableDueToSourcePolicy;
      break;
    case Direction::FROM_NETWORK:
      // Expect the destination IP to match the client.
      ip_parse_result = address_to_check.FromPackedString(
          reinterpret_cast<const char*>(&header->ip6_dst),
          sizeof(header->ip6_src));
      address_reject_code = ICMP6_DST_UNREACH_NOROUTE;
      break;
  }
  DCHECK(ip_parse_result);
  if (!client_ip_.InSameSubnet(address_to_check, client_ip_subnet_length_)) {
    QUIC_DVLOG(1)
        << "Dropped packet: source/destination address is not client's";
    icmp_header->icmp6_type = ICMP6_DST_UNREACH;
    icmp_header->icmp6_code = address_reject_code;
    return ProcessingResult::ICMP;
  }

  // Check and decrement TTL.
  if (header->ip6_hops <= 1) {
    icmp_header->icmp6_type = ICMP6_TIME_EXCEEDED;
    icmp_header->icmp6_code = ICMP6_TIME_EXCEED_TRANSIT;
    return ProcessingResult::ICMP;
  }
  header->ip6_hops--;

  // Check and extract IP headers.
  switch (header->ip6_nxt) {
    case IPPROTO_TCP:
    case IPPROTO_UDP:
    case IPPROTO_ICMPV6:
      *transport_protocol = header->ip6_nxt;
      *transport_data = (&*packet->begin()) + kIPv6HeaderSize;
      break;
    default:
      icmp_header->icmp6_type = ICMP6_PARAM_PROB;
      icmp_header->icmp6_code = ICMP6_PARAMPROB_NEXTHEADER;
      return ProcessingResult::ICMP;
  }

  return ProcessingResult::OK;
}

void QbonePacketProcessor::SendIcmpResponse(
    icmp6_hdr* icmp_header,
    quiche::QuicheStringPiece original_packet,
    Direction original_direction) {
  in6_addr dst;
  // TODO(b/70339814): ensure this is actually a unicast address.
  memcpy(dst.s6_addr, &original_packet[8], kIPv6AddressSize);

  CreateIcmpPacket(
      self_ip_, dst, *icmp_header, original_packet,
      [this, original_direction](quiche::QuicheStringPiece packet) {
        SendResponse(original_direction, packet);
      });
}

void QbonePacketProcessor::SendTcpReset(
    quiche::QuicheStringPiece original_packet,
    Direction original_direction) {
  CreateTcpResetPacket(original_packet, [this, original_direction](
                                            quiche::QuicheStringPiece packet) {
    SendResponse(original_direction, packet);
  });
}

void QbonePacketProcessor::SendResponse(Direction original_direction,
                                        quiche::QuicheStringPiece packet) {
  switch (original_direction) {
    case Direction::FROM_OFF_NETWORK:
      output_->SendPacketToClient(packet);
      break;
    case Direction::FROM_NETWORK:
      output_->SendPacketToNetwork(packet);
      break;
  }
}

}  // namespace quic
