// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"

namespace quic {

QuicConnectionStats::QuicConnectionStats()
    : bytes_sent(0),
      packets_sent(0),
      stream_bytes_sent(0),
      packets_discarded(0),
      bytes_received(0),
      packets_received(0),
      packets_processed(0),
      stream_bytes_received(0),
      bytes_retransmitted(0),
      packets_retransmitted(0),
      bytes_spuriously_retransmitted(0),
      packets_spuriously_retransmitted(0),
      packets_lost(0),
      slowstart_count(0),
      slowstart_num_rtts(0),
      slowstart_packets_sent(0),
      slowstart_bytes_sent(0),
      slowstart_packets_lost(0),
      slowstart_bytes_lost(0),
      slowstart_duration(QuicTime::Delta::Zero()),
      slowstart_start_time(QuicTime::Zero()),
      packets_dropped(0),
      undecryptable_packets_received(0),
      crypto_retransmit_count(0),
      loss_timeout_count(0),
      tlp_count(0),
      rto_count(0),
      pto_count(0),
      min_rtt_us(0),
      srtt_us(0),
      max_packet_size(0),
      max_received_packet_size(0),
      estimated_bandwidth(QuicBandwidth::Zero()),
      packets_reordered(0),
      max_sequence_reordering(0),
      max_time_reordering_us(0),
      tcp_loss_events(0),
      connection_creation_time(QuicTime::Zero()),
      blocked_frames_received(0),
      blocked_frames_sent(0),
      num_connectivity_probing_received(0) {}

QuicConnectionStats::QuicConnectionStats(const QuicConnectionStats& other) =
    default;

QuicConnectionStats::~QuicConnectionStats() {}

std::ostream& operator<<(std::ostream& os, const QuicConnectionStats& s) {
  os << "{ bytes_sent: " << s.bytes_sent;
  os << " packets_sent: " << s.packets_sent;
  os << " stream_bytes_sent: " << s.stream_bytes_sent;
  os << " packets_discarded: " << s.packets_discarded;
  os << " bytes_received: " << s.bytes_received;
  os << " packets_received: " << s.packets_received;
  os << " packets_processed: " << s.packets_processed;
  os << " stream_bytes_received: " << s.stream_bytes_received;
  os << " bytes_retransmitted: " << s.bytes_retransmitted;
  os << " packets_retransmitted: " << s.packets_retransmitted;
  os << " bytes_spuriously_retransmitted: " << s.bytes_spuriously_retransmitted;
  os << " packets_spuriously_retransmitted: "
     << s.packets_spuriously_retransmitted;
  os << " packets_lost: " << s.packets_lost;
  os << " slowstart_packets_sent: " << s.slowstart_packets_sent;
  os << " slowstart_packets_lost: " << s.slowstart_packets_lost;
  os << " slowstart_bytes_lost: " << s.slowstart_bytes_lost;
  os << " packets_dropped: " << s.packets_dropped;
  os << " undecryptable_packets_received: " << s.undecryptable_packets_received;
  os << " crypto_retransmit_count: " << s.crypto_retransmit_count;
  os << " loss_timeout_count: " << s.loss_timeout_count;
  os << " tlp_count: " << s.tlp_count;
  os << " rto_count: " << s.rto_count;
  os << " pto_count: " << s.pto_count;
  os << " min_rtt_us: " << s.min_rtt_us;
  os << " srtt_us: " << s.srtt_us;
  os << " max_packet_size: " << s.max_packet_size;
  os << " max_received_packet_size: " << s.max_received_packet_size;
  os << " estimated_bandwidth: " << s.estimated_bandwidth;
  os << " packets_reordered: " << s.packets_reordered;
  os << " max_sequence_reordering: " << s.max_sequence_reordering;
  os << " max_time_reordering_us: " << s.max_time_reordering_us;
  os << " tcp_loss_events: " << s.tcp_loss_events;
  os << " connection_creation_time: "
     << s.connection_creation_time.ToDebuggingValue();
  os << " blocked_frames_received: " << s.blocked_frames_received;
  os << " blocked_frames_sent: " << s.blocked_frames_sent;
  os << " num_connectivity_probing_received: "
     << s.num_connectivity_probing_received << " }";

  return os;
}

}  // namespace quic
