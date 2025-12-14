// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_STARTUP_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_STARTUP_H_

#include "quiche/quic/core/congestion_control/bbr2_misc.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class Bbr2Sender;
class QUICHE_EXPORT Bbr2StartupMode final : public Bbr2ModeBase {
 public:
  Bbr2StartupMode(const Bbr2Sender* sender, Bbr2NetworkModel* model,
                  QuicTime now);

  void Enter(QuicTime now,
             const Bbr2CongestionEvent* congestion_event) override;
  void Leave(QuicTime now,
             const Bbr2CongestionEvent* congestion_event) override;

  Bbr2Mode OnCongestionEvent(
      QuicByteCount prior_in_flight, QuicTime event_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      const Bbr2CongestionEvent& congestion_event) override;

  Limits<QuicByteCount> GetCwndLimits() const override {
    // Inflight_lo is never set in STARTUP.
    QUICHE_DCHECK_EQ(Bbr2NetworkModel::inflight_lo_default(),
                     model_->inflight_lo());
    return NoGreaterThan(model_->inflight_lo());
  }

  bool IsProbingForBandwidth() const override { return true; }

  Bbr2Mode OnExitQuiescence(QuicTime /*now*/,
                            QuicTime /*quiescence_start_time*/) override {
    return Bbr2Mode::STARTUP;
  }

  struct QUICHE_EXPORT DebugState {
    bool full_bandwidth_reached;
    QuicBandwidth full_bandwidth_baseline = QuicBandwidth::Zero();
    QuicRoundTripCount round_trips_without_bandwidth_growth;
  };

  DebugState ExportDebugState() const;

 private:
  const Bbr2Params& Params() const;

  void CheckExcessiveLosses(const Bbr2CongestionEvent& congestion_event);
  // Used when the pacing gain can decrease in STARTUP.
  QuicBandwidth max_bw_at_round_beginning_ = QuicBandwidth::Zero();
};

QUICHE_EXPORT std::ostream& operator<<(
    std::ostream& os, const Bbr2StartupMode::DebugState& state);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_STARTUP_H_
