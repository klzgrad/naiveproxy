// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONNECTION_STATS_H_
#define QUICHE_QUIC_CORE_QUIC_CONNECTION_STATS_H_

#include <cstdint>
#include <ostream>

#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {
// Structure to hold stats for a QuicConnection.
struct QUIC_EXPORT_PRIVATE QuicConnectionStats {
  QuicConnectionStats();
  QuicConnectionStats(const QuicConnectionStats& other);
  ~QuicConnectionStats();

  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
      const QuicConnectionStats& s);

  QuicByteCount bytes_sent;  // Includes retransmissions.
  QuicPacketCount packets_sent;
  // Non-retransmitted bytes sent in a stream frame.
  QuicByteCount stream_bytes_sent;
  // Packets serialized and discarded before sending.
  QuicPacketCount packets_discarded;

  // These include version negotiation and public reset packets, which do not
  // have packet numbers or frame data.
  QuicByteCount bytes_received;  // Includes duplicate data for a stream.
  // Includes packets which were not processable.
  QuicPacketCount packets_received;
  // Excludes packets which were not processable.
  QuicPacketCount packets_processed;
  QuicByteCount stream_bytes_received;  // Bytes received in a stream frame.

  QuicByteCount bytes_retransmitted;
  QuicPacketCount packets_retransmitted;

  QuicByteCount bytes_spuriously_retransmitted;
  QuicPacketCount packets_spuriously_retransmitted;
  // Number of packets abandoned as lost by the loss detection algorithm.
  QuicPacketCount packets_lost;

  // Number of times this connection went through the slow start phase.
  uint32_t slowstart_count;
  // Number of round trips spent in slow start.
  uint32_t slowstart_num_rtts;
  // Number of packets sent in slow start.
  QuicPacketCount slowstart_packets_sent;
  // Number of bytes sent in slow start.
  QuicByteCount slowstart_bytes_sent;
  // Number of packets lost exiting slow start.
  QuicPacketCount slowstart_packets_lost;
  // Number of bytes lost exiting slow start.
  QuicByteCount slowstart_bytes_lost;
  // Time spent in COMPLETED slow start phases.
  QuicTime::Delta slowstart_duration;
  // Start time of the last slow start phase.
  QuicTime slowstart_start_time;

  QuicPacketCount packets_dropped;  // Duplicate or less than least unacked.

  // Packets that failed to decrypt when they were first received.
  QuicPacketCount undecryptable_packets_received;

  size_t crypto_retransmit_count;
  // Count of times the loss detection alarm fired.  At least one packet should
  // be lost when the alarm fires.
  size_t loss_timeout_count;
  size_t tlp_count;
  size_t rto_count;  // Count of times the rto timer fired.
  size_t pto_count;

  int64_t min_rtt_us;  // Minimum RTT in microseconds.
  int64_t srtt_us;     // Smoothed RTT in microseconds.
  QuicByteCount max_packet_size;
  QuicByteCount max_received_packet_size;
  QuicBandwidth estimated_bandwidth;

  // Reordering stats for received packets.
  // Number of packets received out of packet number order.
  QuicPacketCount packets_reordered;
  // Maximum reordering observed in packet number space.
  QuicPacketCount max_sequence_reordering;
  // Maximum reordering observed in microseconds
  int64_t max_time_reordering_us;

  // The following stats are used only in TcpCubicSender.
  // The number of loss events from TCP's perspective.  Each loss event includes
  // one or more lost packets.
  uint32_t tcp_loss_events;

  // Creation time, as reported by the QuicClock.
  QuicTime connection_creation_time;

  uint64_t blocked_frames_received;
  uint64_t blocked_frames_sent;

  // Number of connectivity probing packets received by this connection.
  uint64_t num_connectivity_probing_received;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_STATS_H_
