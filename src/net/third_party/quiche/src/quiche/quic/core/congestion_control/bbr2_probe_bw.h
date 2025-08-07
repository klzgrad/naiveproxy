// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_PROBE_BW_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_PROBE_BW_H_

#include <cstdint>

#include "quiche/quic/core/congestion_control/bbr2_misc.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

class Bbr2Sender;
class QUICHE_EXPORT Bbr2ProbeBwMode final : public Bbr2ModeBase {
 public:
  using Bbr2ModeBase::Bbr2ModeBase;

  void Enter(QuicTime now,
             const Bbr2CongestionEvent* congestion_event) override;
  void Leave(QuicTime /*now*/,
             const Bbr2CongestionEvent* /*congestion_event*/) override {}

  Bbr2Mode OnCongestionEvent(
      QuicByteCount prior_in_flight, QuicTime event_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      const Bbr2CongestionEvent& congestion_event) override;

  Limits<QuicByteCount> GetCwndLimits() const override;

  bool IsProbingForBandwidth() const override;

  Bbr2Mode OnExitQuiescence(QuicTime now,
                            QuicTime quiescence_start_time) override;

  enum class CyclePhase : uint8_t {
    PROBE_NOT_STARTED,
    PROBE_UP,
    PROBE_DOWN,
    PROBE_CRUISE,
    PROBE_REFILL,
  };

  static const char* CyclePhaseToString(CyclePhase phase);

  struct QUICHE_EXPORT DebugState {
    CyclePhase phase;
    QuicTime cycle_start_time = QuicTime::Zero();
    QuicTime phase_start_time = QuicTime::Zero();
  };

  DebugState ExportDebugState() const;

 private:
  const Bbr2Params& Params() const;
  float PacingGainForPhase(CyclePhase phase) const;

  void UpdateProbeUp(QuicByteCount prior_in_flight,
                     const Bbr2CongestionEvent& congestion_event);
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

  // Return whether adapted inflight_hi. If inflight is too high, this function
  // will not adapt inflight_hi and will return false.
  AdaptUpperBoundsResult MaybeAdaptUpperBounds(
      const Bbr2CongestionEvent& congestion_event);

  void EnterProbeDown(bool probed_too_high, bool stopped_risky_probe,
                      QuicTime now);
  void EnterProbeCruise(QuicTime now);
  void EnterProbeRefill(uint64_t probe_up_rounds, QuicTime now);
  void EnterProbeUp(QuicTime now);

  // Call right before the exit of PROBE_DOWN.
  void ExitProbeDown();

  float PercentTimeElapsedToProbeBandwidth(
      const Bbr2CongestionEvent& congestion_event) const;

  bool IsTimeToProbeBandwidth(
      const Bbr2CongestionEvent& congestion_event) const;
  bool HasStayedLongEnoughInProbeDown(
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

  struct QUICHE_EXPORT Cycle {
    QuicTime cycle_start_time = QuicTime::Zero();
    CyclePhase phase = CyclePhase::PROBE_NOT_STARTED;
    uint64_t rounds_in_phase = 0;
    QuicTime phase_start_time = QuicTime::Zero();
    QuicRoundTripCount rounds_since_probe = 0;
    QuicTime::Delta probe_wait_time = QuicTime::Delta::Zero();
    uint64_t probe_up_rounds = 0;
    QuicByteCount probe_up_bytes = std::numeric_limits<QuicByteCount>::max();
    QuicByteCount probe_up_acked = 0;
    bool probe_up_app_limited_since_inflight_hi_limited_ = false;
    // Whether max bandwidth filter window has advanced in this cycle. It is
    // advanced once per cycle.
    bool has_advanced_max_bw = false;
    bool is_sample_from_probing = false;
  } cycle_;

  bool last_cycle_probed_too_high_;
  bool last_cycle_stopped_risky_probe_;
};

QUICHE_EXPORT std::ostream& operator<<(
    std::ostream& os, const Bbr2ProbeBwMode::DebugState& state);

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const Bbr2ProbeBwMode::CyclePhase phase);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_PROBE_BW_H_
