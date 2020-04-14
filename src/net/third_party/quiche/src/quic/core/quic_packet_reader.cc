// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_reader.h"

#include <errno.h>
#ifndef __APPLE__
// This is a GNU header that is not present on Apple platforms
#include <features.h>
#endif
#include <string.h>
#include <sys/socket.h>

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

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

namespace quic {

QuicPacketReader::QuicPacketReader()
    : read_buffers_(kNumPacketsPerReadMmsgCall),
      read_results_(kNumPacketsPerReadMmsgCall) {
  if (!remove_quic_socket_utils_from_packet_reader_) {
    Initialize();
    return;
  }

  QUIC_RESTART_FLAG_COUNT_N(quic_remove_quic_socket_utils_from_packet_reader, 1,
                            5);
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

void QuicPacketReader::Initialize() {
#if MMSG_MORE
  // Zero initialize uninitialized memory.
  memset(mmsg_hdr_, 0, sizeof(mmsg_hdr_));

  for (int i = 0; i < kNumPacketsPerReadMmsgCall; ++i) {
    packets_[i].iov.iov_base = packets_[i].buf;
    packets_[i].iov.iov_len = sizeof(packets_[i].buf);
    memset(&packets_[i].raw_address, 0, sizeof(packets_[i].raw_address));
    memset(packets_[i].cbuf, 0, sizeof(packets_[i].cbuf));
    memset(packets_[i].buf, 0, sizeof(packets_[i].buf));

    msghdr* hdr = &mmsg_hdr_[i].msg_hdr;
    hdr->msg_name = &packets_[i].raw_address;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    hdr->msg_iov = &packets_[i].iov;
    hdr->msg_iovlen = 1;

    hdr->msg_control = packets_[i].cbuf;
    hdr->msg_controllen = kCmsgSpaceForReadPacket;
  }
#endif
}

QuicPacketReader::~QuicPacketReader() = default;

bool QuicPacketReader::ReadAndDispatchPackets(
    int fd,
    int port,
    const QuicClock& clock,
    ProcessPacketInterface* processor,
    QuicPacketCount* packets_dropped) {
  if (!remove_quic_socket_utils_from_packet_reader_) {
#if MMSG_MORE_NO_ANDROID
    return ReadAndDispatchManyPackets(fd, port, clock, processor,
                                      packets_dropped);
#else
    return ReadAndDispatchSinglePacket(fd, port, clock, processor,
                                       packets_dropped);
#endif
  }

  // Reset all read_results for reuse.
  for (size_t i = 0; i < read_results_.size(); ++i) {
    read_results_[i].Reset(
        /*packet_buffer_length=*/sizeof(read_buffers_[i].packet_buffer));
  }
  QuicWallTime wallnow = clock.WallNow();
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
      QUIC_RESTART_FLAG_COUNT_N(
          quic_remove_quic_socket_utils_from_packet_reader, 2, 5);
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

    QuicWallTime walltimestamp =
        result.packet_info.HasValue(QuicUdpPacketInfoBit::RECV_TIMESTAMP)
            ? result.packet_info.receive_timestamp()
            : wallnow;
    if (!result.packet_info.HasValue(QuicUdpPacketInfoBit::RECV_TIMESTAMP)) {
      QUIC_RESTART_FLAG_COUNT_N(
          quic_remove_quic_socket_utils_from_packet_reader, 3, 5);
    }

    bool has_ttl = result.packet_info.HasValue(QuicUdpPacketInfoBit::TTL);
    int ttl = has_ttl ? result.packet_info.ttl() : 0;
    if (!has_ttl) {
      QUIC_RESTART_FLAG_COUNT_N(
          quic_remove_quic_socket_utils_from_packet_reader, 4, 5);
    }

    char* headers = nullptr;
    size_t headers_length = 0;
    if (result.packet_info.HasValue(
            QuicUdpPacketInfoBit::GOOGLE_PACKET_HEADER)) {
      headers = result.packet_info.google_packet_headers().buffer;
      headers_length = result.packet_info.google_packet_headers().buffer_len;
    } else {
      QUIC_RESTART_FLAG_COUNT_N(
          quic_remove_quic_socket_utils_from_packet_reader, 5, 5);
    }

    QuicReceivedPacket packet(
        result.packet_buffer.buffer, result.packet_buffer.buffer_len,
        clock.ConvertWallTimeToQuicTime(walltimestamp), /*owns_buffer=*/false,
        ttl, has_ttl, headers, headers_length, /*owns_header_buffer=*/false);

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

bool QuicPacketReader::ReadAndDispatchManyPackets(
    int fd,
    int port,
    const QuicClock& clock,
    ProcessPacketInterface* processor,
    QuicPacketCount* packets_dropped) {
  DCHECK(!remove_quic_socket_utils_from_packet_reader_);
#if MMSG_MORE_NO_ANDROID
  // Re-set the length fields in case recvmmsg has changed them.
  for (int i = 0; i < kNumPacketsPerReadMmsgCall; ++i) {
    DCHECK_LE(kMaxOutgoingPacketSize, packets_[i].iov.iov_len);
    msghdr* hdr = &mmsg_hdr_[i].msg_hdr;
    hdr->msg_namelen = sizeof(sockaddr_storage);
    DCHECK_EQ(1u, hdr->msg_iovlen);
    hdr->msg_controllen = kCmsgSpaceForReadPacket;
    hdr->msg_flags = 0;
  }

  int packets_read =
      recvmmsg(fd, mmsg_hdr_, kNumPacketsPerReadMmsgCall, MSG_TRUNC, nullptr);

  if (packets_read <= 0) {
    return false;  // recvmmsg failed.
  }

  bool use_quic_time =
      GetQuicReloadableFlag(quic_use_quic_time_for_received_timestamp);
  QuicTime fallback_timestamp(QuicTime::Zero());
  QuicWallTime fallback_walltimestamp = QuicWallTime::Zero();
  for (int i = 0; i < packets_read; ++i) {
    if (mmsg_hdr_[i].msg_len == 0) {
      continue;
    }

    if (QUIC_PREDICT_FALSE(mmsg_hdr_[i].msg_hdr.msg_flags & MSG_CTRUNC)) {
      QUIC_BUG << "Incorrectly set control length: "
               << mmsg_hdr_[i].msg_hdr.msg_controllen << ", expected "
               << kCmsgSpaceForReadPacket;
      continue;
    }

    if (QUIC_PREDICT_FALSE(mmsg_hdr_[i].msg_hdr.msg_flags & MSG_TRUNC)) {
      QUIC_LOG_FIRST_N(WARNING, 100)
          << "Dropping truncated QUIC packet: buffer size:"
          << packets_[i].iov.iov_len << " packet size:" << mmsg_hdr_[i].msg_len;
      QUIC_SERVER_HISTOGRAM_COUNTS(
          "QuicPacketReader.DroppedPacketSize", mmsg_hdr_[i].msg_len, 1, 10000,
          20, "In QuicPacketReader, the size of big packets that are dropped.");
      continue;
    }

    QuicSocketAddress peer_address(packets_[i].raw_address);
    QuicIpAddress self_ip;
    QuicWallTime packet_walltimestamp = QuicWallTime::Zero();
    QuicSocketUtils::GetAddressAndTimestampFromMsghdr(
        &mmsg_hdr_[i].msg_hdr, &self_ip, &packet_walltimestamp);
    if (!self_ip.IsInitialized()) {
      QUIC_BUG << "Unable to get self IP address.";
      continue;
    }

    // This isn't particularly desirable, but not all platforms support socket
    // timestamping.
    QuicTime timestamp(QuicTime::Zero());
    if (!use_quic_time) {
      if (packet_walltimestamp.IsZero()) {
        if (fallback_walltimestamp.IsZero()) {
          fallback_walltimestamp = clock.WallNow();
        }
        packet_walltimestamp = fallback_walltimestamp;
      }
      timestamp = clock.ConvertWallTimeToQuicTime(packet_walltimestamp);

    } else {
      QUIC_RELOADABLE_FLAG_COUNT(quic_use_quic_time_for_received_timestamp);
      if (packet_walltimestamp.IsZero()) {
        if (!fallback_timestamp.IsInitialized()) {
          fallback_timestamp = clock.Now();
        }
        timestamp = fallback_timestamp;
      } else {
        timestamp = clock.ConvertWallTimeToQuicTime(packet_walltimestamp);
      }
    }
    int ttl = 0;
    bool has_ttl =
        QuicSocketUtils::GetTtlFromMsghdr(&mmsg_hdr_[i].msg_hdr, &ttl);
    char* headers = nullptr;
    size_t headers_length = 0;
    QuicSocketUtils::GetPacketHeadersFromMsghdr(&mmsg_hdr_[i].msg_hdr, &headers,
                                                &headers_length);
    QuicReceivedPacket packet(reinterpret_cast<char*>(packets_[i].iov.iov_base),
                              mmsg_hdr_[i].msg_len, timestamp, false, ttl,
                              has_ttl, headers, headers_length, false);
    QuicSocketAddress self_address(self_ip, port);
    processor->ProcessPacket(self_address, peer_address, packet);
  }

  if (packets_dropped != nullptr) {
    QuicSocketUtils::GetOverflowFromMsghdr(&mmsg_hdr_[0].msg_hdr,
                                           packets_dropped);
  }

  // We may not have read all of the packets available on the socket.
  return packets_read == kNumPacketsPerReadMmsgCall;
#else
  (void)fd;
  (void)port;
  (void)clock;
  (void)processor;
  (void)packets_dropped;
  QUIC_LOG(FATAL) << "Unsupported";
  return false;
#endif
}

/* static */
bool QuicPacketReader::ReadAndDispatchSinglePacket(
    int fd,
    int port,
    const QuicClock& clock,
    ProcessPacketInterface* processor,
    QuicPacketCount* packets_dropped) {
  DCHECK(!GetQuicRestartFlag(quic_remove_quic_socket_utils_from_packet_reader));
  char buf[kMaxV4PacketSize];

  QuicSocketAddress peer_address;
  QuicIpAddress self_ip;
  QuicWallTime walltimestamp = QuicWallTime::Zero();
  int bytes_read = QuicSocketUtils::ReadPacket(fd, buf, QUICHE_ARRAYSIZE(buf),
                                               packets_dropped, &self_ip,
                                               &walltimestamp, &peer_address);
  if (bytes_read < 0) {
    return false;  // ReadPacket failed.
  }

  if (!self_ip.IsInitialized()) {
    QUIC_BUG << "Unable to get self IP address.";
    return false;
  }
  // This isn't particularly desirable, but not all platforms support socket
  // timestamping.
  if (walltimestamp.IsZero()) {
    walltimestamp = clock.WallNow();
  }
  QuicTime timestamp = clock.ConvertWallTimeToQuicTime(walltimestamp);

  QuicReceivedPacket packet(buf, bytes_read, timestamp, false);
  QuicSocketAddress self_address(self_ip, port);
  processor->ProcessPacket(self_address, peer_address, packet);

  // The socket read was successful, so return true even if packet dispatch
  // failed.
  return true;
}

}  // namespace quic
