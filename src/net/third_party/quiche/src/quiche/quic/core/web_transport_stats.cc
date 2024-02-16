// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/web_transport_stats.h"

#include "absl/time/time.h"
#include "quiche/quic/core/congestion_control/rtt_stats.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

webtransport::DatagramStats WebTransportDatagramStatsForQuicSession(
    const QuicSession& session) {
  webtransport::DatagramStats result;
  result.expired_outgoing = session.expired_datagrams_in_default_queue();
  result.lost_outgoing = session.total_datagrams_lost();
  return result;
}

webtransport::SessionStats WebTransportStatsForQuicSession(
    const QuicSession& session) {
  const RttStats* rtt_stats =
      session.connection()->sent_packet_manager().GetRttStats();
  webtransport::SessionStats result;
  result.min_rtt = rtt_stats->min_rtt().ToAbsl();
  result.smoothed_rtt = rtt_stats->smoothed_rtt().ToAbsl();
  result.rtt_variation = rtt_stats->mean_deviation().ToAbsl();
  result.estimated_send_rate_bps = session.connection()
                                       ->sent_packet_manager()
                                       .BandwidthEstimate()
                                       .ToBitsPerSecond();
  result.datagram_stats = WebTransportDatagramStatsForQuicSession(session);
  return result;
}

}  // namespace quic
