// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_MISC_H_
#define QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_MISC_H_

#include <algorithm>
#include <limits>

#include "net/third_party/quiche/src/quic/core/congestion_control/bandwidth_sampler.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/windowed_filter.h"
#include "net/third_party/quiche/src/quic/core/quic_bandwidth.h"
#include "net/third_party/quiche/src/quic/core/quic_packet_number.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/quic/platform/impl/quic_export_impl.h"

namespace quic {

template <typename T>
class QUIC_EXPORT_PRIVATE Limits {
 public:
  Limits(T min, T max) : min_(min), max_(max) {}

  // If [min, max] is an empty range, i.e. min > max, this function returns max,
  // because typically a value larger than max means "risky".
  T ApplyLimits(T raw_value) const {
    return std::min(max_, std::max(min_, raw_value));
  }

  T Min() const { return min_; }
  T Max() const { return max_; }

 private:
  T min_;
  T max_;
};

template <typename T>
QUIC_EXPORT_PRIVATE inline Limits<T> MinMax(T min, T max) {
  return Limits<T>(min, max);
}

template <typename T>
QUIC_EXPORT_PRIVATE inline Limits<T> NoLessThan(T min) {
  return Limits<T>(min, std::numeric_limits<T>::max());
}

template <typename T>
QUIC_EXPORT_PRIVATE inline Limits<T> NoGreaterThan(T max) {
  return Limits<T>(std::numeric_limits<T>::min(), max);
}

template <typename T>
QUIC_EXPORT_PRIVATE inline Limits<T> Unlimited() {
  return Limits<T>(std::numeric_limits<T>::min(),
                   std::numeric_limits<T>::max());
}

template <typename T>
QUIC_EXPORT_PRIVATE inline std::ostream& operator<<(std::ostream& os,
                                                    const Limits<T>& limits) {
  return os << "[" << limits.Min() << ", " << limits.Max() << "]";
}

// Bbr2Params contains all parameters of a Bbr2Sender.
struct QUIC_EXPORT_PRIVATE Bbr2Params {
  Bbr2Params(QuicByteCount cwnd_min, QuicByteCount cwnd_max)
      : cwnd_limits(cwnd_min, cwnd_max) {}

  /*
   * STARTUP parameters.
   */

  // The gain for both CWND and PacingRate at startup.
  // TODO(wub): Maybe change to the newly derived value of 2.773 (4 * ln(2)).
  float startup_gain = 2.885;

  // Full bandwidth is declared if the total bandwidth growth is less than
  // |startup_full_bw_threshold| times in the last |startup_full_bw_rounds|
  // round trips.
  float startup_full_bw_threshold = 1.25;

  QuicRoundTripCount startup_full_bw_rounds = 3;

  // The minimum number of loss marking events to exit STARTUP.
  int64_t startup_full_loss_count =
      GetQuicFlag(FLAGS_quic_bbr2_default_startup_full_loss_count);

  /*
   * DRAIN parameters.
   */
  float drain_cwnd_gain = 2.885;
  float drain_pacing_gain = 1.0 / 2.885;

  /*
   * PROBE_BW parameters.
   */
  // Max amount of randomness to inject in round counting for Reno-coexistence.
  QuicRoundTripCount probe_bw_max_probe_rand_rounds = 2;

  // Max number of rounds before probing for Reno-coexistence.
  uint32_t probe_bw_probe_max_rounds = 63;

  // Multiplier to get Reno-style probe epoch duration as: k * BDP round trips.
  // If zero, disables Reno-style BDP-scaled coexistence mechanism.
  float probe_bw_probe_reno_gain = 1.0;

  // Minimum duration for BBR-native probes.
  QuicTime::Delta probe_bw_probe_base_duration =
      QuicTime::Delta::FromMilliseconds(
          GetQuicFlag(FLAGS_quic_bbr2_default_probe_bw_base_duration_ms));

  // The upper bound of the random amount of BBR-native probes.
  QuicTime::Delta probe_bw_probe_max_rand_duration =
      QuicTime::Delta::FromMilliseconds(
          GetQuicFlag(FLAGS_quic_bbr2_default_probe_bw_max_rand_duration_ms));

  // The minimum number of loss marking events to exit the PROBE_UP phase.
  int64_t probe_bw_full_loss_count =
      GetQuicFlag(FLAGS_quic_bbr2_default_probe_bw_full_loss_count);

  // Multiplier to get target inflight (as multiple of BDP) for PROBE_UP phase.
  float probe_bw_probe_inflight_gain = 1.25;

  // Pacing gains.
  float probe_bw_probe_up_pacing_gain = 1.25;
  float probe_bw_probe_down_pacing_gain = 0.75;
  float probe_bw_default_pacing_gain = 1.0;

  float probe_bw_cwnd_gain = 2.0;

  /*
   * PROBE_RTT parameters.
   */
  float probe_rtt_inflight_target_bdp_fraction = 0.5;
  QuicTime::Delta probe_rtt_period = QuicTime::Delta::FromMilliseconds(
      GetQuicFlag(FLAGS_quic_bbr2_default_probe_rtt_period_ms));
  QuicTime::Delta probe_rtt_duration = QuicTime::Delta::FromMilliseconds(200);

  /*
   * Parameters used by multiple modes.
   */

  // The initial value of the max ack height filter's window length.
  QuicRoundTripCount initial_max_ack_height_filter_window =
      GetQuicFlag(FLAGS_quic_bbr2_default_initial_ack_height_filter_window);

  // Fraction of unutilized headroom to try to leave in path upon high loss.
  float inflight_hi_headroom =
      GetQuicFlag(FLAGS_quic_bbr2_default_inflight_hi_headroom);

  // Estimate startup/bw probing has gone too far if loss rate exceeds this.
  float loss_threshold = GetQuicFlag(FLAGS_quic_bbr2_default_loss_threshold);

  // A common factor for multiplicative decreases. Used for adjusting
  // bandwidth_lo, inflight_lo and inflight_hi upon losses.
  float beta = 0.3;

  Limits<QuicByteCount> cwnd_limits;

  /*
   * Experimental flags from QuicConfig.
   */

  // Indicates app-limited calls should be ignored as long as there's
  // enough data inflight to see more bandwidth when necessary.
  bool flexible_app_limited = false;

  // Can be disabled by connection option 'B2NA'.
  bool add_ack_height_to_queueing_threshold =
      GetQuicReloadableFlag(quic_bbr2_add_ack_height_to_queueing_threshold);

  // Can be disabled by connection option 'B2RP'.
  bool avoid_unnecessary_probe_rtt =
      GetQuicReloadableFlag(quic_bbr2_avoid_unnecessary_probe_rtt);
};

class QUIC_EXPORT_PRIVATE RoundTripCounter {
 public:
  RoundTripCounter();

  QuicRoundTripCount Count() const { return round_trip_count_; }

  QuicPacketNumber last_sent_packet() const { return last_sent_packet_; }

  // Must be called in ascending packet number order.
  void OnPacketSent(QuicPacketNumber packet_number);

  // Return whether a round trip has just completed.
  bool OnPacketsAcked(QuicPacketNumber last_acked_packet);

  void RestartRound();

 private:
  QuicRoundTripCount round_trip_count_;
  QuicPacketNumber last_sent_packet_;
  // The last sent packet number of the current round trip.
  QuicPacketNumber end_of_round_trip_;
};

class QUIC_EXPORT_PRIVATE MinRttFilter {
 public:
  MinRttFilter(QuicTime::Delta initial_min_rtt,
               QuicTime initial_min_rtt_timestamp);

  void Update(QuicTime::Delta sample_rtt, QuicTime now);

  void ForceUpdate(QuicTime::Delta sample_rtt, QuicTime now);

  QuicTime::Delta Get() const { return min_rtt_; }

  QuicTime GetTimestamp() const { return min_rtt_timestamp_; }

 private:
  QuicTime::Delta min_rtt_;
  // Time when the current value of |min_rtt_| was assigned.
  QuicTime min_rtt_timestamp_;
};

class QUIC_EXPORT_PRIVATE Bbr2MaxBandwidthFilter {
 public:
  void Update(QuicBandwidth sample) {
    max_bandwidth_[1] = std::max(sample, max_bandwidth_[1]);
  }

  void Advance() {
    if (max_bandwidth_[1].IsZero()) {
      return;
    }

    max_bandwidth_[0] = max_bandwidth_[1];
    max_bandwidth_[1] = QuicBandwidth::Zero();
  }

  QuicBandwidth Get() const {
    return std::max(max_bandwidth_[0], max_bandwidth_[1]);
  }

 private:
  QuicBandwidth max_bandwidth_[2] = {QuicBandwidth::Zero(),
                                     QuicBandwidth::Zero()};
};

// Information that are meaningful only when Bbr2Sender::OnCongestionEvent is
// running.
struct QUIC_EXPORT_PRIVATE Bbr2CongestionEvent {
  QuicTime event_time = QuicTime::Zero();

  // The congestion window prior to the processing of the ack/loss events.
  QuicByteCount prior_cwnd;

  // Total bytes inflight before the processing of the ack/loss events.
  QuicByteCount prior_bytes_in_flight = 0;

  // Total bytes inflight after the processing of the ack/loss events.
  QuicByteCount bytes_in_flight = 0;

  // Total bytes acked from acks in this event.
  QuicByteCount bytes_acked = 0;

  // Total bytes lost from losses in this event.
  QuicByteCount bytes_lost = 0;

  // Whether acked_packets indicates the end of a round trip.
  bool end_of_round_trip = false;

  // TODO(wub): After deprecating --quic_one_bw_sample_per_ack_event, use
  // last_packet_send_state.is_app_limited instead of this field.
  // Whether the last bandwidth sample from acked_packets is app limited.
  // false if acked_packets is empty.
  bool last_sample_is_app_limited = false;

  // When the event happened, whether the sender is probing for bandwidth.
  bool is_probing_for_bandwidth = false;

  // Minimum rtt of all bandwidth samples from acked_packets.
  // QuicTime::Delta::Infinite() if acked_packets is empty.
  QuicTime::Delta sample_min_rtt = QuicTime::Delta::Infinite();

  // Maximum bandwidth of all bandwidth samples from acked_packets.
  QuicBandwidth sample_max_bandwidth = QuicBandwidth::Zero();

  // The send state of the largest packet in acked_packets, unless it is empty.
  // If acked_packets is empty, it's the send state of the largest packet in
  // lost_packets.
  SendTimeState last_packet_send_state;
};

// Bbr2NetworkModel takes low level congestion signals(packets sent/acked/lost)
// as input and produces BBRv2 model parameters like inflight_(hi|lo),
// bandwidth_(hi|lo), bandwidth and rtt estimates, etc.
class QUIC_EXPORT_PRIVATE Bbr2NetworkModel {
 public:
  Bbr2NetworkModel(const Bbr2Params* params,
                   QuicTime::Delta initial_rtt,
                   QuicTime initial_rtt_timestamp,
                   float cwnd_gain,
                   float pacing_gain,
                   const BandwidthSampler* old_sampler);

  void OnPacketSent(QuicTime sent_time,
                    QuicByteCount bytes_in_flight,
                    QuicPacketNumber packet_number,
                    QuicByteCount bytes,
                    HasRetransmittableData is_retransmittable);

  void OnCongestionEventStart(QuicTime event_time,
                              const AckedPacketVector& acked_packets,
                              const LostPacketVector& lost_packets,
                              Bbr2CongestionEvent* congestion_event);

  void OnCongestionEventFinish(QuicPacketNumber least_unacked_packet,
                               const Bbr2CongestionEvent& congestion_event);

  // Update the model without a congestion event.
  // Max bandwidth is updated if |bandwidth| is larger than existing max
  // bandwidth. Min rtt is updated if |rtt| is non-zero and smaller than
  // existing min rtt.
  void UpdateNetworkParameters(QuicBandwidth bandwidth, QuicTime::Delta rtt);

  // Update inflight/bandwidth short-term lower bounds.
  void AdaptLowerBounds(const Bbr2CongestionEvent& congestion_event);

  // Restart the current round trip as if it is starting now.
  void RestartRound();

  void AdvanceMaxBandwidthFilter() { max_bandwidth_filter_.Advance(); }

  void OnApplicationLimited() { bandwidth_sampler_.OnAppLimited(); }

  QuicByteCount BDP(QuicBandwidth bandwidth) const {
    return bandwidth * MinRtt();
  }

  QuicByteCount BDP(QuicBandwidth bandwidth, float gain) const {
    return bandwidth * MinRtt() * gain;
  }

  QuicTime::Delta MinRtt() const { return min_rtt_filter_.Get(); }

  QuicTime MinRttTimestamp() const { return min_rtt_filter_.GetTimestamp(); }

  // TODO(wub): If we do this too frequently, we can potentailly postpone
  // PROBE_RTT indefinitely. Observe how it works in production and improve it.
  void PostponeMinRttTimestamp(QuicTime::Delta duration) {
    min_rtt_filter_.ForceUpdate(MinRtt(), MinRttTimestamp() + duration);
  }

  QuicBandwidth MaxBandwidth() const { return max_bandwidth_filter_.Get(); }

  QuicByteCount MaxAckHeight() const {
    return bandwidth_sampler_.max_ack_height();
  }

  void EnableOverestimateAvoidance() {
    bandwidth_sampler_.EnableOverestimateAvoidance();
  }

  void OnPacketNeutered(QuicPacketNumber packet_number) {
    bandwidth_sampler_.OnPacketNeutered(packet_number);
  }

  uint64_t num_ack_aggregation_epochs() const {
    return bandwidth_sampler_.num_ack_aggregation_epochs();
  }

  bool MaybeExpireMinRtt(const Bbr2CongestionEvent& congestion_event);

  QuicBandwidth BandwidthEstimate() const {
    return std::min(MaxBandwidth(), bandwidth_lo_);
  }

  QuicRoundTripCount RoundTripCount() const {
    return round_trip_counter_.Count();
  }

  bool IsCongestionWindowLimited(
      const Bbr2CongestionEvent& congestion_event) const;

  // TODO(wub): Replace this by a new version which takes two thresholds, one
  // is the number of loss events, the other is the percentage of bytes lost.
  bool IsInflightTooHigh(const Bbr2CongestionEvent& congestion_event) const;

  QuicPacketNumber last_sent_packet() const {
    return round_trip_counter_.last_sent_packet();
  }

  QuicByteCount total_bytes_acked() const {
    return bandwidth_sampler_.total_bytes_acked();
  }

  QuicByteCount total_bytes_lost() const {
    return bandwidth_sampler_.total_bytes_lost();
  }

  QuicByteCount total_bytes_sent() const {
    return bandwidth_sampler_.total_bytes_sent();
  }

  int64_t loss_events_in_round() const { return loss_events_in_round_; }

  QuicPacketNumber end_of_app_limited_phase() const {
    return bandwidth_sampler_.end_of_app_limited_phase();
  }

  QuicBandwidth bandwidth_latest() const { return bandwidth_latest_; }
  QuicBandwidth bandwidth_lo() const { return bandwidth_lo_; }
  static QuicBandwidth bandwidth_lo_default() {
    return QuicBandwidth::Infinite();
  }
  void clear_bandwidth_lo() { bandwidth_lo_ = bandwidth_lo_default(); }

  QuicByteCount inflight_latest() const { return inflight_latest_; }
  QuicByteCount inflight_lo() const { return inflight_lo_; }
  static QuicByteCount inflight_lo_default() {
    return std::numeric_limits<QuicByteCount>::max();
  }
  void clear_inflight_lo() { inflight_lo_ = inflight_lo_default(); }
  void cap_inflight_lo(QuicByteCount cap) {
    if (inflight_lo_ != inflight_lo_default() && inflight_lo_ > cap) {
      inflight_lo_ = cap;
    }
  }

  QuicByteCount inflight_hi_with_headroom() const;
  QuicByteCount inflight_hi() const { return inflight_hi_; }
  static QuicByteCount inflight_hi_default() {
    return std::numeric_limits<QuicByteCount>::max();
  }
  void set_inflight_hi(QuicByteCount inflight_hi) {
    inflight_hi_ = inflight_hi;
  }

  float cwnd_gain() const { return cwnd_gain_; }
  void set_cwnd_gain(float cwnd_gain) { cwnd_gain_ = cwnd_gain; }

  float pacing_gain() const { return pacing_gain_; }
  void set_pacing_gain(float pacing_gain) { pacing_gain_ = pacing_gain; }

 private:
  const Bbr2Params& Params() const { return *params_; }
  const Bbr2Params* const params_;
  RoundTripCounter round_trip_counter_;

  // Bandwidth sampler provides BBR with the bandwidth measurements at
  // individual points.
  BandwidthSampler bandwidth_sampler_;
  // The filter that tracks the maximum bandwidth over multiple recent round
  // trips.
  Bbr2MaxBandwidthFilter max_bandwidth_filter_;
  MinRttFilter min_rtt_filter_;

  // Bytes lost in the current round. Updated once per congestion event.
  QuicByteCount bytes_lost_in_round_ = 0;
  // Number of loss marking events in the current round.
  int64_t loss_events_in_round_ = 0;

  // Max bandwidth in the current round. Updated once per congestion event.
  QuicBandwidth bandwidth_latest_ = QuicBandwidth::Zero();
  // Max bandwidth of recent rounds. Updated once per round.
  QuicBandwidth bandwidth_lo_ = bandwidth_lo_default();

  // Max inflight in the current round. Updated once per congestion event.
  QuicByteCount inflight_latest_ = 0;
  // Max inflight of recent rounds. Updated once per round.
  QuicByteCount inflight_lo_ = inflight_lo_default();
  QuicByteCount inflight_hi_ = inflight_hi_default();

  float cwnd_gain_;
  float pacing_gain_;

  const bool fix_zero_bw_on_loss_only_event_ =
      GetQuicReloadableFlag(quic_bbr_fix_zero_bw_on_loss_only_event);
};

enum class Bbr2Mode : uint8_t {
  // Startup phase of the connection.
  STARTUP,
  // After achieving the highest possible bandwidth during the startup, lower
  // the pacing rate in order to drain the queue.
  DRAIN,
  // Cruising mode.
  PROBE_BW,
  // Temporarily slow down sending in order to empty the buffer and measure
  // the real minimum RTT.
  PROBE_RTT,
};

QUIC_EXPORT_PRIVATE inline std::ostream& operator<<(std::ostream& os,
                                                    const Bbr2Mode& mode) {
  switch (mode) {
    case Bbr2Mode::STARTUP:
      return os << "STARTUP";
    case Bbr2Mode::DRAIN:
      return os << "DRAIN";
    case Bbr2Mode::PROBE_BW:
      return os << "PROBE_BW";
    case Bbr2Mode::PROBE_RTT:
      return os << "PROBE_RTT";
  }
  return os << "<Invalid Mode>";
}

// The base class for all BBRv2 modes. A Bbr2Sender is in one mode at a time,
// this interface is used to implement mode-specific behaviors.
class Bbr2Sender;
class QUIC_EXPORT_PRIVATE Bbr2ModeBase {
 public:
  Bbr2ModeBase(const Bbr2Sender* sender, Bbr2NetworkModel* model)
      : sender_(sender), model_(model) {}

  virtual ~Bbr2ModeBase() = default;

  // Called when entering/leaving this mode.
  // congestion_event != nullptr means BBRv2 is switching modes in the context
  // of a ack and/or loss.
  virtual void Enter(QuicTime now,
                     const Bbr2CongestionEvent* congestion_event) = 0;
  virtual void Leave(QuicTime now,
                     const Bbr2CongestionEvent* congestion_event) = 0;

  virtual Bbr2Mode OnCongestionEvent(
      QuicByteCount prior_in_flight,
      QuicTime event_time,
      const AckedPacketVector& acked_packets,
      const LostPacketVector& lost_packets,
      const Bbr2CongestionEvent& congestion_event) = 0;

  virtual Limits<QuicByteCount> GetCwndLimits() const = 0;

  virtual bool IsProbingForBandwidth() const = 0;

  virtual Bbr2Mode OnExitQuiescence(QuicTime now,
                                    QuicTime quiescence_start_time) = 0;

 protected:
  const Bbr2Sender* const sender_;
  Bbr2NetworkModel* model_;
};

QUIC_EXPORT_PRIVATE inline QuicByteCount BytesInFlight(
    const SendTimeState& send_state) {
  DCHECK(send_state.is_valid);
  if (send_state.bytes_in_flight != 0) {
    return send_state.bytes_in_flight;
  }
  return send_state.total_bytes_sent - send_state.total_bytes_acked -
         send_state.total_bytes_lost;
}

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONGESTION_CONTROL_BBR2_MISC_H_
