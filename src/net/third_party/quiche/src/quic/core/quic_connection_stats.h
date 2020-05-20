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
#include "net/third_party/quiche/src/quic/core/quic_time_accumulator.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// Structure to hold stats for a QuicConnection.
struct QUIC_EXPORT_PRIVATE QuicConnectionStats {
  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
      const QuicConnectionStats& s);

  QuicByteCount bytes_sent = 0;  // Includes retransmissions.
  QuicPacketCount packets_sent = 0;
  // Non-retransmitted bytes sent in a stream frame.
  QuicByteCount stream_bytes_sent = 0;
  // Packets serialized and discarded before sending.
  QuicPacketCount packets_discarded = 0;

  // These include version negotiation and public reset packets, which do not
  // have packet numbers or frame data.
  QuicByteCount bytes_received = 0;  // Includes duplicate data for a stream.
  // Includes packets which were not processable.
  QuicPacketCount packets_received = 0;
  // Excludes packets which were not processable.
  QuicPacketCount packets_processed = 0;
  QuicByteCount stream_bytes_received = 0;  // Bytes received in a stream frame.

  QuicByteCount bytes_retransmitted = 0;
  QuicPacketCount packets_retransmitted = 0;

  QuicByteCount bytes_spuriously_retransmitted = 0;
  QuicPacketCount packets_spuriously_retransmitted = 0;
  // Number of packets abandoned as lost by the loss detection algorithm.
  QuicPacketCount packets_lost = 0;
  QuicPacketCount packet_spuriously_detected_lost = 0;

  // The sum of the detection time of all lost packets. The detection time of a
  // lost packet is defined as: T(detection) - T(send).
  QuicTime::Delta total_loss_detection_time = QuicTime::Delta::Zero();

  // Number of times this connection went through the slow start phase.
  uint32_t slowstart_count = 0;
  // Number of round trips spent in slow start.
  uint32_t slowstart_num_rtts = 0;
  // Number of packets sent in slow start.
  QuicPacketCount slowstart_packets_sent = 0;
  // Number of bytes sent in slow start.
  QuicByteCount slowstart_bytes_sent = 0;
  // Number of packets lost exiting slow start.
  QuicPacketCount slowstart_packets_lost = 0;
  // Number of bytes lost exiting slow start.
  QuicByteCount slowstart_bytes_lost = 0;
  // Time spent in slow start. Populated for BBRv1 and BBRv2.
  QuicTimeAccumulator slowstart_duration;

  // Number of PROBE_BW cycles. Populated for BBRv1 and BBRv2.
  uint32_t bbr_num_cycles = 0;
  // Number of PROBE_BW cycles shortened for reno coexistence. BBRv2 only.
  uint32_t bbr_num_short_cycles_for_reno_coexistence = 0;
  // Whether BBR exited STARTUP due to excessive loss. Populated for BBRv1 and
  // BBRv2.
  bool bbr_exit_startup_due_to_loss = false;

  QuicPacketCount packets_dropped = 0;  // Duplicate or less than least unacked.

  // Packets that failed to decrypt when they were first received,
  // before the handshake was complete.
  QuicPacketCount undecryptable_packets_received_before_handshake_complete = 0;

  size_t crypto_retransmit_count = 0;
  // Count of times the loss detection alarm fired.  At least one packet should
  // be lost when the alarm fires.
  size_t loss_timeout_count = 0;
  size_t tlp_count = 0;
  size_t rto_count = 0;  // Count of times the rto timer fired.
  size_t pto_count = 0;

  int64_t min_rtt_us = 0;  // Minimum RTT in microseconds.
  int64_t srtt_us = 0;     // Smoothed RTT in microseconds.
  QuicByteCount max_packet_size = 0;
  QuicByteCount max_received_packet_size = 0;
  QuicBandwidth estimated_bandwidth = QuicBandwidth::Zero();

  // Reordering stats for received packets.
  // Number of packets received out of packet number order.
  QuicPacketCount packets_reordered = 0;
  // Maximum reordering observed in packet number space.
  QuicPacketCount max_sequence_reordering = 0;
  // Maximum reordering observed in microseconds
  int64_t max_time_reordering_us = 0;

  // The following stats are used only in TcpCubicSender.
  // The number of loss events from TCP's perspective.  Each loss event includes
  // one or more lost packets.
  uint32_t tcp_loss_events = 0;

  // Creation time, as reported by the QuicClock.
  QuicTime connection_creation_time = QuicTime::Zero();

  uint64_t blocked_frames_received = 0;
  uint64_t blocked_frames_sent = 0;

  // Number of connectivity probing packets received by this connection.
  uint64_t num_connectivity_probing_received = 0;

  // Whether a RETRY packet was successfully processed.
  bool retry_packet_processed = false;

  // Number of received coalesced packets.
  uint64_t num_coalesced_packets_received = 0;
  // Number of successfully processed coalesced packets.
  uint64_t num_coalesced_packets_processed = 0;
  // Number of ack aggregation epochs. For the same number of bytes acked, the
  // smaller this value, the more ack aggregation is going on.
  uint64_t num_ack_aggregation_epochs = 0;

  // Whether overshooting is detected (and pacing rate decreases) during start
  // up with network parameters adjusted.
  bool overshooting_detected_with_network_parameters_adjusted = false;

  // Whether there is any non app-limited bandwidth sample.
  bool has_non_app_limited_sample = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONNECTION_STATS_H_
