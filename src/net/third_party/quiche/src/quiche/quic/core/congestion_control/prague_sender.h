// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A modification of Cubic to match Prague congestion control, as described in
// draft-briscoe-iccrg-prague-congestion-control-04.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_PRAGUE_SENDER_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_PRAGUE_SENDER_H_

#include <optional>

#include "quiche/quic/core/congestion_control/rtt_stats.h"
#include "quiche/quic/core/congestion_control/send_algorithm_interface.h"
#include "quiche/quic/core/congestion_control/tcp_cubic_sender_bytes.h"
#include "quiche/quic/core/quic_connection_stats.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

class RttStats;

constexpr float kPragueEwmaGain = 1 / 16.0;
constexpr QuicTime::Delta kPragueRttVirtMin =
    QuicTime::Delta::FromMilliseconds(25);
constexpr int kRoundsBeforeReducedRttDependence = 500;

namespace test {
class PragueSenderPeer;
}  // namespace test

class QUICHE_EXPORT PragueSender : public TcpCubicSenderBytes {
 public:
  PragueSender(const QuicClock* clock, const RttStats* rtt_stats,
               QuicPacketCount initial_tcp_congestion_window,
               QuicPacketCount max_congestion_window,
               QuicConnectionStats* stats);
  PragueSender(const PragueSender&) = delete;
  PragueSender& operator=(const PragueSender&) = delete;
  ~PragueSender() override {}

  // Start implementation of SendAlgorithmInterface overrides.
  void OnCongestionEvent(bool rtt_updated, QuicByteCount prior_in_flight,
                         QuicTime event_time,
                         const AckedPacketVector& acked_packets,
                         const LostPacketVector& lost_packets,
                         QuicPacketCount num_ect,
                         QuicPacketCount num_ce) override;
  CongestionControlType GetCongestionControlType() const override;
  bool EnableECT1() override;
  // End implementation of SendAlgorithmInterface overrides.

 private:
  friend class test::PragueSenderPeer;

  bool ect1_enabled_ = false;

  // Tracks the life of the connection to begin reducing RTT dependence of
  // congestion avoidance after 500 RTTs.
  QuicTime connection_start_time_;
  bool reduce_rtt_dependence_ = false;

  // Alpha-related variables
  std::optional<float> prague_alpha_;
  QuicPacketCount ect_count_ = 0;
  QuicPacketCount ce_count_ = 0;

  // Virtual RTT related variables
  QuicTime::Delta rtt_virt_ = kPragueRttVirtMin;
  QuicTime last_alpha_update_;

  // Accounting for recent CE-based cwnd reductions that are "credit" for future
  // loss responses.
  std::optional<QuicTime> last_congestion_response_time_;
  QuicByteCount last_congestion_response_size_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_PRAGUE_SENDER_H_
