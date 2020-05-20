// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_SENDER_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_SENDER_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/congestion_control/bandwidth_sampler.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_drain.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_misc.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_probe_bw.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_probe_rtt.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr2_startup.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/bbr_sender.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/windowed_filter.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

class QUIC_EXPORT_PRIVATE Bbr2Sender final : public SendAlgorithmInterface {
 public:
  Bbr2Sender(QuicTime now,
             const RttStats* rtt_stats,
             const QuicUnackedPacketMap* unacked_packets,
             QuicPacketCount initial_cwnd_in_packets,
             QuicPacketCount max_cwnd_in_packets,
             QuicRandom* random,
             QuicConnectionStats* stats,
             BbrSender* old_sender);

  ~Bbr2Sender() override = default;

  // Start implementation of SendAlgorithmInterface.
  bool InSlowStart() const override { return mode_ == Bbr2Mode::STARTUP; }

  bool InRecovery() const override {
    // TODO(wub): Implement Recovery.
    return false;
  }

  bool ShouldSendProbingPacket() const override;

  void SetFromConfig(const QuicConfig& config,
                     Perspective perspective) override;

  void AdjustNetworkParameters(const NetworkParams& params) override;

  void SetInitialCongestionWindowInPackets(
      QuicPacketCount congestion_window) override;

  void OnCongestionEvent(bool rtt_updated,
                         QuicByteCount prior_in_flight,
                         QuicTime event_time,
                         const AckedPacketVector& acked_packets,
                         const LostPacketVector& lost_packets) override;

  void OnPacketSent(QuicTime sent_time,
                    QuicByteCount bytes_in_flight,
                    QuicPacketNumber packet_number,
                    QuicByteCount bytes,
                    HasRetransmittableData is_retransmittable) override;

  void OnPacketNeutered(QuicPacketNumber packet_number) override;

  void OnRetransmissionTimeout(bool /*packets_retransmitted*/) override {}

  void OnConnectionMigration() override {}

  bool CanSend(QuicByteCount bytes_in_flight) override;

  QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const override;

  QuicBandwidth BandwidthEstimate() const override {
    return model_.BandwidthEstimate();
  }

  QuicByteCount GetCongestionWindow() const override;

  QuicByteCount GetSlowStartThreshold() const override { return 0; }

  CongestionControlType GetCongestionControlType() const override {
    return kBBRv2;
  }

  std::string GetDebugState() const override;

  void OnApplicationLimited(QuicByteCount bytes_in_flight) override;

  void PopulateConnectionStats(QuicConnectionStats* stats) const override;
  // End implementation of SendAlgorithmInterface.

  const Bbr2Params& Params() const { return params_; }

  QuicByteCount GetMinimumCongestionWindow() const {
    return cwnd_limits().Min();
  }

  // Returns the min of BDP and congestion window.
  QuicByteCount GetTargetBytesInflight() const;

  struct QUIC_EXPORT_PRIVATE DebugState {
    Bbr2Mode mode;

    // Shared states.
    QuicRoundTripCount round_trip_count;
    QuicBandwidth bandwidth_hi = QuicBandwidth::Zero();
    QuicBandwidth bandwidth_lo = QuicBandwidth::Zero();
    QuicBandwidth bandwidth_est = QuicBandwidth::Zero();
    QuicByteCount inflight_hi;
    QuicByteCount inflight_lo;
    QuicByteCount max_ack_height;
    QuicTime::Delta min_rtt = QuicTime::Delta::Zero();
    QuicTime min_rtt_timestamp = QuicTime::Zero();
    QuicByteCount congestion_window;
    QuicBandwidth pacing_rate = QuicBandwidth::Zero();
    bool last_sample_is_app_limited;
    QuicPacketNumber end_of_app_limited_phase;

    // Mode-specific debug states.
    Bbr2StartupMode::DebugState startup;
    Bbr2DrainMode::DebugState drain;
    Bbr2ProbeBwMode::DebugState probe_bw;
    Bbr2ProbeRttMode::DebugState probe_rtt;
  };

  DebugState ExportDebugState() const;

 private:
  void UpdatePacingRate(QuicByteCount bytes_acked);
  void UpdateCongestionWindow(QuicByteCount bytes_acked);
  QuicByteCount GetTargetCongestionWindow(float gain) const;
  void OnEnterQuiescence(QuicTime now);
  void OnExitQuiescence(QuicTime now);

  // Helper function for BBR2_MODE_DISPATCH.
  Bbr2ProbeRttMode& probe_rtt_or_die() {
    DCHECK_EQ(mode_, Bbr2Mode::PROBE_RTT);
    return probe_rtt_;
  }

  const Bbr2ProbeRttMode& probe_rtt_or_die() const {
    DCHECK_EQ(mode_, Bbr2Mode::PROBE_RTT);
    return probe_rtt_;
  }

  uint64_t RandomUint64(uint64_t max) const {
    return random_->RandUint64() % max;
  }

  // Returns true if there are enough bytes in flight to ensure more bandwidth
  // will be observed if present.
  bool IsPipeSufficientlyFull() const;

  // Cwnd limits imposed by the current Bbr2 mode.
  Limits<QuicByteCount> GetCwndLimitsByMode() const;

  // Cwnd limits imposed by caller.
  const Limits<QuicByteCount>& cwnd_limits() const;

  const Bbr2Params& params() const { return params_; }

  Bbr2Mode mode_;

  const RttStats* const rtt_stats_;
  const QuicUnackedPacketMap* const unacked_packets_;
  QuicRandom* random_;
  QuicConnectionStats* connection_stats_;

  // Don't use it directly outside of SetFromConfig. Instead, use params() to
  // get read-only access.
  Bbr2Params params_;

  Bbr2NetworkModel model_;

  const QuicByteCount initial_cwnd_;

  // Current cwnd and pacing rate.
  QuicByteCount cwnd_;
  QuicBandwidth pacing_rate_;

  QuicTime last_quiescence_start_ = QuicTime::Zero();

  Bbr2StartupMode startup_;
  Bbr2DrainMode drain_;
  Bbr2ProbeBwMode probe_bw_;
  Bbr2ProbeRttMode probe_rtt_;

  // Debug only.
  bool last_sample_is_app_limited_;

  friend class Bbr2StartupMode;
  friend class Bbr2DrainMode;
  friend class Bbr2ProbeBwMode;
  friend class Bbr2ProbeRttMode;
};

QUIC_EXPORT_PRIVATE std::ostream& operator<<(
    std::ostream& os,
    const Bbr2Sender::DebugState& state);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_SENDER_H_
