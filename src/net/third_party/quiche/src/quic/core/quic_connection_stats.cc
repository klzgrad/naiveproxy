// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"

namespace quic {

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
  os << " undecryptable_packets_received_before_handshake_complete: "
     << s.undecryptable_packets_received_before_handshake_complete;
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
     << s.num_connectivity_probing_received;
  os << " retry_packet_processed: "
     << (s.retry_packet_processed ? "yes" : "no");
  os << " num_coalesced_packets_received: " << s.num_coalesced_packets_received;
  os << " num_coalesced_packets_processed: "
     << s.num_coalesced_packets_processed;
  os << " num_ack_aggregation_epochs: " << s.num_ack_aggregation_epochs;
  os << " }";

  return os;
}

}  // namespace quic
