// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR3_SENDER_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR3_SENDER_H_

#include <cstdint>

#include "quiche/quic/core/congestion_control/bandwidth_sampler.h"
#include "quiche/quic/core/congestion_control/bbr2_misc.h"
#include "quiche/quic/core/congestion_control/bbr_sender.h"
#include "quiche/quic/core/congestion_control/rtt_stats.h"
#include "quiche/quic/core/congestion_control/send_algorithm_interface.h"
#include "quiche/quic/core/congestion_control/windowed_filter.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

class QUICHE_EXPORT Bbr3Sender final : public SendAlgorithmInterface {
 public:
  Bbr3Sender(QuicTime now, const RttStats* rtt_stats,
             const QuicUnackedPacketMap* unacked_packets,
             QuicPacketCount initial_cwnd_in_packets,
             QuicPacketCount max_cwnd_in_packets, QuicRandom* random,
             QuicConnectionStats* stats, BbrSender* old_sender);

  ~Bbr3Sender() override = default;

  // Start implementation of SendAlgorithmInterface.
  bool InSlowStart() const override { return mode_ == Bbr2Mode::STARTUP; }

  bool InRecovery() const override {
    // TODO(wub): Implement Recovery.
    return false;
  }

  void SetFromConfig(const QuicConfig& config,
                     Perspective perspective) override;

  void ApplyConnectionOptions(const QuicTagVector& connection_options) override;

  void AdjustNetworkParameters(const NetworkParams& params) override;

  void SetInitialCongestionWindowInPackets(
      QuicPacketCount congestion_window) override;

  void SetApplicationDrivenPacingRate(
      QuicBandwidth application_bandwidth_target) override;

  void OnCongestionEvent(bool rtt_updated, QuicByteCount prior_in_flight,
                         QuicTime event_time,
                         const AckedPacketVector& acked_packets,
                         const LostPacketVector& lost_packets,
                         QuicPacketCount num_ect,
                         QuicPacketCount num_ce) override;

  void OnPacketSent(QuicTime sent_time, QuicByteCount bytes_in_flight,
                    QuicPacketNumber packet_number, QuicByteCount bytes,
                    HasRetransmittableData is_retransmittable) override;

  void OnPacketNeutered(QuicPacketNumber packet_number) override;

  void OnRetransmissionTimeout(bool /*packets_retransmitted*/) override {}

  void OnConnectionMigration() override {}

  bool CanSend(QuicByteCount bytes_in_flight) override;

  QuicBandwidth PacingRate(QuicByteCount bytes_in_flight) const override;

  QuicBandwidth BandwidthEstimate() const override {
    return model_.BandwidthEstimate();
  }

  bool HasGoodBandwidthEstimateForResumption() const override {
    return has_non_app_limited_sample_;
  }

  QuicByteCount GetCongestionWindow() const override;

  QuicByteCount GetSlowStartThreshold() const override { return 0; }

  CongestionControlType GetCongestionControlType() const override {
    return kBBRv3;
  }

  std::string GetDebugState() const override;

  void OnApplicationLimited(QuicByteCount bytes_in_flight) override;

  void PopulateConnectionStats(QuicConnectionStats* stats) const override;

  bool EnableECT0() override { return false; }
  bool EnableECT1() override { return false; }
  void ReduceMemoryUsage() override { model_.ReduceMemoryUsage(); }
  // End implementation of SendAlgorithmInterface.

  const Bbr2Params& Params() const { return params_; }

  QuicByteCount GetMinimumCongestionWindow() const {
    return params_.cwnd_limits.Min();
  }

  // Returns the min of BDP and congestion window.
  QuicByteCount GetTargetBytesInflight() const;

  bool IsBandwidthOverestimateAvoidanceEnabled() const {
    return model_.IsBandwidthOverestimateAvoidanceEnabled();
  }

  Bbr2DebugState ExportDebugState() const;

  const Bbr2NetworkModel& GetNetworkModel() const { return model_; }

 private:
  void UpdatePacingRate(QuicByteCount bytes_acked);
  void UpdateCongestionWindow(QuicByteCount bytes_acked);
  QuicByteCount GetTargetCongestionWindow(float gain) const;
  // Helper function for Bbr2Mode transitions.
  void LeaveStartup(QuicTime now);
  Bbr2Mode OnCongestionEventStartup(
      const Bbr2CongestionEvent& congestion_event);
  void CheckExcessiveLosses(const Bbr2CongestionEvent& congestion_event);

  Bbr2Mode OnCongestionEventDrain(const Bbr2CongestionEvent& congestion_event);
  QuicByteCount DrainTarget() const;

  void OnExitQuiescence(QuicTime now);

  Bbr2Mode OnCongestionEventProbeBw(
      QuicByteCount prior_in_flight, QuicTime event_time,
      const Bbr2CongestionEvent& congestion_event);

  void UpdateProbeUp(const Bbr2CongestionEvent& congestion_event);
  void UpdateProbeDown(QuicByteCount prior_in_flight,
                       const Bbr2CongestionEvent& congestion_event);
  void UpdateProbeCruise(const Bbr2CongestionEvent& congestion_event);
  void UpdateProbeRefill(const Bbr2CongestionEvent& congestion_event);

  enum AdaptUpperBoundsResult : uint8_t {
    ADAPTED_OK,
    ADAPTED_PROBED_TOO_HIGH,
    NOT_ADAPTED_INFLIGHT_HIGH_NOT_SET,
    NOT_ADAPTED_INVALID_SAMPLE,
  };

  AdaptUpperBoundsResult MaybeAdaptUpperBounds(
      const Bbr2CongestionEvent& congestion_event);

  void EnterProbeDown(bool probed_too_high, bool stopped_risky_probe,
                      QuicTime now);
  void EnterProbeCruise(QuicTime now);
  void EnterProbeRefill(uint64_t probe_up_rounds, QuicTime now);
  void EnterProbeUp(QuicTime now);
  void ExitProbeDown();

  bool IsTimeToProbeBandwidth(
      const Bbr2CongestionEvent& congestion_event) const;
  bool HasCycleLasted(QuicTime::Delta duration,
                      const Bbr2CongestionEvent& congestion_event) const;
  bool HasPhaseLasted(QuicTime::Delta duration,
                      const Bbr2CongestionEvent& congestion_event) const;
  bool IsTimeToProbeForRenoCoexistence(
      double probe_wait_fraction,
      const Bbr2CongestionEvent& congestion_event) const;

  void RaiseInflightHighSlope();
  void ProbeInflightHighUpward(const Bbr2CongestionEvent& congestion_event);
  float PacingGainForPhase(ProbePhase phase) const;

  void EnterProbeRtt();
  Bbr2Mode OnCongestionEventProbeRtt(
      const Bbr2CongestionEvent& congestion_event);
  QuicByteCount InflightTarget() const;

  uint64_t RandomUint64(uint64_t max) const {
    return random_->RandUint64() % max;
  }

  // Cwnd limits imposed by the current Bbr2 mode.
  Limits<QuicByteCount> GetCwndLimitsByMode() const;

  // Cwnd limits imposed by caller.

  Bbr2Mode mode_;

  const QuicUnackedPacketMap* const unacked_packets_;
  QuicRandom* random_;
  QuicConnectionStats* connection_stats_;

  // Don't use it directly outside of SetFromConfig and ApplyConnectionOptions.
  // Instead, use params() to get read-only access.
  Bbr2Params params_;

  Bbr2NetworkModel model_;

  const QuicByteCount initial_cwnd_;

  // Current cwnd and pacing rate.
  QuicByteCount cwnd_;
  QuicBandwidth pacing_rate_;

  QuicTime last_quiescence_start_ = QuicTime::Zero();

  // Max congestion window when adjusting network parameters.
  QuicByteCount max_cwnd_when_network_parameters_adjusted_ =
      kMaxInitialCongestionWindow * kDefaultTCPMSS;

  // Startup state.
  QuicBandwidth startup_max_bw_at_round_beginning_ = QuicBandwidth::Zero();

  // Probe BW state.
  struct ProbeBWState {
    QuicTime cycle_start_time = QuicTime::Zero();
    ProbePhase phase = ProbePhase::PROBE_NOT_STARTED;
    uint64_t rounds_in_phase = 0;
    QuicTime phase_start_time = QuicTime::Zero();
    QuicRoundTripCount rounds_since_probe = 0;
    QuicTime::Delta probe_wait_time = QuicTime::Delta::Zero();
    uint64_t probe_up_rounds = 0;
    QuicByteCount probe_up_bytes = std::numeric_limits<QuicByteCount>::max();
    QuicByteCount probe_up_acked = 0;
    // Whether max bandwidth filter window has advanced in this cycle. It is
    // advanced once per cycle.
    bool has_advanced_max_bw = false;
    bool is_sample_from_probing = false;

    bool last_cycle_probed_too_high = false;
    bool last_cycle_stopped_risky_probe = false;
  } probe_bw_;

  // Probe RTT state.
  struct ProbeRTTState {
    QuicTime exit_time = QuicTime::Zero();
  } probe_rtt_;

  bool has_non_app_limited_sample_ = false;

  // Debug only.
  bool last_sample_is_app_limited_;
};


}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR3_SENDER_H_
