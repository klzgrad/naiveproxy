// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_reader.h"

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_process_packet_interface.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_server_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {

QuicPacketReader::QuicPacketReader()
    : read_buffers_(kNumPacketsPerReadMmsgCall),
      read_results_(kNumPacketsPerReadMmsgCall) {
  DCHECK_EQ(read_buffers_.size(), read_results_.size());
  for (size_t i = 0; i < read_results_.size(); ++i) {
    read_results_[i].packet_buffer.buffer = read_buffers_[i].packet_buffer;
    read_results_[i].packet_buffer.buffer_len =
        sizeof(read_buffers_[i].packet_buffer);

    read_results_[i].control_buffer.buffer = read_buffers_[i].control_buffer;
    read_results_[i].control_buffer.buffer_len =
        sizeof(read_buffers_[i].control_buffer);
  }
}

QuicPacketReader::~QuicPacketReader() = default;

bool QuicPacketReader::ReadAndDispatchPackets(
    int fd,
    int port,
    const QuicClock& clock,
    ProcessPacketInterface* processor,
    QuicPacketCount* /*packets_dropped*/) {
  // Reset all read_results for reuse.
  for (size_t i = 0; i < read_results_.size(); ++i) {
    read_results_[i].Reset(
        /*packet_buffer_length=*/sizeof(read_buffers_[i].packet_buffer));
  }

  // Use clock.Now() as the packet receipt time, the time between packet
  // arriving at the host and now is considered part of the network delay.
  QuicTime now = clock.Now();

  size_t packets_read = socket_api_.ReadMultiplePackets(
      fd,
      BitMask64(QuicUdpPacketInfoBit::DROPPED_PACKETS,
                QuicUdpPacketInfoBit::PEER_ADDRESS,
                QuicUdpPacketInfoBit::V4_SELF_IP,
                QuicUdpPacketInfoBit::V6_SELF_IP,
                QuicUdpPacketInfoBit::RECV_TIMESTAMP, QuicUdpPacketInfoBit::TTL,
                QuicUdpPacketInfoBit::GOOGLE_PACKET_HEADER),
      &read_results_);
  for (size_t i = 0; i < packets_read; ++i) {
    auto& result = read_results_[i];
    if (!result.ok) {
      QUIC_CODE_COUNT(quic_packet_reader_read_failure);
      continue;
    }

    if (!result.packet_info.HasValue(QuicUdpPacketInfoBit::PEER_ADDRESS)) {
      QUIC_BUG << "Unable to get peer socket address.";
      continue;
    }

    QuicSocketAddress peer_address =
        result.packet_info.peer_address().Normalized();

    QuicIpAddress self_ip = GetSelfIpFromPacketInfo(
        result.packet_info, peer_address.host().IsIPv6());
    if (!self_ip.IsInitialized()) {
      QUIC_BUG << "Unable to get self IP address.";
      continue;
    }

    bool has_ttl = result.packet_info.HasValue(QuicUdpPacketInfoBit::TTL);
    int ttl = has_ttl ? result.packet_info.ttl() : 0;
    if (!has_ttl) {
      QUIC_CODE_COUNT(quic_packet_reader_no_ttl);
    }

    char* headers = nullptr;
    size_t headers_length = 0;
    if (result.packet_info.HasValue(
            QuicUdpPacketInfoBit::GOOGLE_PACKET_HEADER)) {
      headers = result.packet_info.google_packet_headers().buffer;
      headers_length = result.packet_info.google_packet_headers().buffer_len;
    } else {
      QUIC_CODE_COUNT(quic_packet_reader_no_google_packet_header);
    }

    QuicReceivedPacket packet(
        result.packet_buffer.buffer, result.packet_buffer.buffer_len, now,
        /*owns_buffer=*/false, ttl, has_ttl, headers, headers_length,
        /*owns_header_buffer=*/false);

    QuicSocketAddress self_address(self_ip, port);
    processor->ProcessPacket(self_address, peer_address, packet);
  }

  // We may not have read all of the packets available on the socket.
  return packets_read == kNumPacketsPerReadMmsgCall;
}

// static
QuicIpAddress QuicPacketReader::GetSelfIpFromPacketInfo(
    const QuicUdpPacketInfo& packet_info,
    bool prefer_v6_ip) {
  if (prefer_v6_ip) {
    if (packet_info.HasValue(QuicUdpPacketInfoBit::V6_SELF_IP)) {
      return packet_info.self_v6_ip();
    }
    if (packet_info.HasValue(QuicUdpPacketInfoBit::V4_SELF_IP)) {
      return packet_info.self_v4_ip();
    }
  } else {
    if (packet_info.HasValue(QuicUdpPacketInfoBit::V4_SELF_IP)) {
      return packet_info.self_v4_ip();
    }
    if (packet_info.HasValue(QuicUdpPacketInfoBit::V6_SELF_IP)) {
      return packet_info.self_v6_ip();
    }
  }
  return QuicIpAddress();
}

}  // namespace quic
