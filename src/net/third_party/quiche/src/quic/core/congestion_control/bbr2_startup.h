// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_STARTUP_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_STARTUP_H_

#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

class Bbr2Sender;
class QUIC_EXPORT_PRIVATE Bbr2StartupMode final : public Bbr2ModeBase {
 public:
  Bbr2StartupMode(const Bbr2Sender* sender,
                  Bbr2NetworkModel* model,
                  QuicTime now);

  void Enter(QuicTime now,
             const Bbr2CongestionEvent* congestion_event) override;
  void Leave(QuicTime now,
             const Bbr2CongestionEvent* congestion_event) override;

  Bbr2Mode OnCongestionEvent(
      QuicByteCount prior_in_flight,
      QuicTime event_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      const Bbr2CongestionEvent& congestion_event) override;

  Limits<QuicByteCount> GetCwndLimits() const override {
    return NoGreaterThan(model_->inflight_lo());
  }

  bool IsProbingForBandwidth() const override { return true; }

  Bbr2Mode OnExitQuiescence(QuicTime /*now*/,
                            QuicTime /*quiescence_start_time*/) override {
    return Bbr2Mode::STARTUP;
  }

  bool FullBandwidthReached() const { return full_bandwidth_reached_; }

  struct QUIC_EXPORT_PRIVATE DebugState {
    bool full_bandwidth_reached;
    QuicBandwidth full_bandwidth_baseline = QuicBandwidth::Zero();
    QuicRoundTripCount round_trips_without_bandwidth_growth;
  };

  DebugState ExportDebugState() const;

 private:
  const Bbr2Params& Params() const;

  void CheckFullBandwidthReached(const Bbr2CongestionEvent& congestion_event);

  void CheckExcessiveLosses(const Bbr2CongestionEvent& congestion_event);

  bool full_bandwidth_reached_;
  QuicBandwidth full_bandwidth_baseline_;
  QuicRoundTripCount rounds_without_bandwidth_growth_;
};

QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    const Bbr2StartupMode::DebugState& state);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_STARTUP_H_
