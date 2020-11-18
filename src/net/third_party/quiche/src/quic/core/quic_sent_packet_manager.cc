// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_sent_packet_manager.h"

#include <algorithm>
#include <string>

#include "net/third_party/quiche/src/quic/core/congestion_control/general_loss_algorithm.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/pacing_sender.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_stats.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"

namespace quic {

namespace {
static const int64_t kDefaultRetransmissionTimeMs = 500;
static const int64_t kMaxRetransmissionTimeMs = 60000;
// Maximum number of exponential backoffs used for RTO timeouts.
static const size_t kMaxRetransmissions = 10;
// Maximum number of packets retransmitted upon an RTO.
static const size_t kMaxRetransmissionsOnTimeout = 2;
// The path degrading delay is the sum of this number of consecutive RTO delays.
const size_t kNumRetransmissionDelaysForPathDegradingDelay = 2;

// Ensure the handshake timer isnt't faster than 10ms.
// This limits the tenth retransmitted packet to 10s after the initial CHLO.
static const int64_t kMinHandshakeTimeoutMs = 10;

// Sends up to two tail loss probes before firing an RTO,
// per draft RFC draft-dukkipati-tcpm-tcp-loss-probe.
static const size_t kDefaultMaxTailLossProbes = 2;

// Returns true of retransmissions of the specified type should retransmit
// the frames directly (as opposed to resulting in a loss notification).
inline bool ShouldForceRetransmission(TransmissionType transmission_type) {
  return transmission_type == HANDSHAKE_RETRANSMISSION ||
         transmission_type == TLP_RETRANSMISSION ||
         transmission_type == PROBING_RETRANSMISSION ||
         transmission_type == RTO_RETRANSMISSION ||
         transmission_type == PTO_RETRANSMISSION;
}

// If pacing rate is accurate, > 2 burst token is not likely to help first ACK
// to arrive earlier, and overly large burst token could cause incast packet
// losses.
static const uint32_t kConservativeUnpacedBurst = 2;

}  // namespace

#define ENDPOINT                                                         \
  (unacked_packets_.perspective() == Perspective::IS_SERVER ? "Server: " \
                                                            : "Client: ")

QuicSentPacketManager::QuicSentPacketManager(
    Perspective perspective,
    const QuicClock* clock,
    QuicRandom* random,
    QuicConnectionStats* stats,
    CongestionControlType congestion_control_type)
    : unacked_packets_(perspective),
      clock_(clock),
      random_(random),
      stats_(stats),
      debug_delegate_(nullptr),
      network_change_visitor_(nullptr),
      initial_congestion_window_(kInitialCongestionWindow),
      loss_algorithm_(&uber_loss_algorithm_),
      consecutive_rto_count_(0),
      consecutive_tlp_count_(0),
      consecutive_crypto_retransmission_count_(0),
      pending_timer_transmission_count_(0),
      max_tail_loss_probes_(kDefaultMaxTailLossProbes),
      max_rto_packets_(kMaxRetransmissionsOnTimeout),
      enable_half_rtt_tail_loss_probe_(false),
      using_pacing_(false),
      use_new_rto_(false),
      conservative_handshake_retransmits_(false),
      min_tlp_timeout_(
          QuicTime::Delta::FromMilliseconds(kMinTailLossProbeTimeoutMs)),
      min_rto_timeout_(
          QuicTime::Delta::FromMilliseconds(kMinRetransmissionTimeMs)),
      largest_mtu_acked_(0),
      handshake_finished_(false),
      peer_max_ack_delay_(
          QuicTime::Delta::FromMilliseconds(kDefaultDelayedAckTimeMs)),
      rtt_updated_(false),
      acked_packets_iter_(last_ack_frame_.packets.rbegin()),
      pto_enabled_(GetQuicReloadableFlag(quic_default_on_pto)),
      max_probe_packets_per_pto_(2),
      consecutive_pto_count_(0),
      handshake_mode_disabled_(false),
      skip_packet_number_for_pto_(false),
      always_include_max_ack_delay_for_pto_timeout_(true),
      pto_exponential_backoff_start_point_(0),
      pto_rttvar_multiplier_(4),
      num_tlp_timeout_ptos_(0),
      handshake_packet_acked_(false),
      zero_rtt_packet_acked_(false),
      one_rtt_packet_acked_(false),
      one_rtt_packet_sent_(false),
      first_pto_srtt_multiplier_(0),
      use_standard_deviation_for_pto_(false),
      pto_multiplier_without_rtt_samples_(3) {
  SetSendAlgorithm(congestion_control_type);
  if (pto_enabled_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_default_on_pto, 1, 2);
    // TODO(fayang): change the default values when deprecating
    // quic_default_on_pto.
    first_pto_srtt_multiplier_ = 1.5;
    pto_rttvar_multiplier_ = 2;
  }
}

QuicSentPacketManager::~QuicSentPacketManager() {}

void QuicSentPacketManager::SetFromConfig(const QuicConfig& config) {
  const Perspective perspective = unacked_packets_.perspective();
  if (config.HasReceivedInitialRoundTripTimeUs() &&
      config.ReceivedInitialRoundTripTimeUs() > 0) {
    if (!config.HasClientSentConnectionOption(kNRTT, perspective)) {
      SetInitialRtt(QuicTime::Delta::FromMicroseconds(
          config.ReceivedInitialRoundTripTimeUs()));
    }
  } else if (config.HasInitialRoundTripTimeUsToSend() &&
             config.GetInitialRoundTripTimeUsToSend() > 0) {
    SetInitialRtt(QuicTime::Delta::FromMicroseconds(
        config.GetInitialRoundTripTimeUsToSend()));
  }
  if (config.HasReceivedMaxAckDelayMs()) {
    peer_max_ack_delay_ =
        QuicTime::Delta::FromMilliseconds(config.ReceivedMaxAckDelayMs());
  }
  if (config.HasClientSentConnectionOption(kMAD0, perspective)) {
    rtt_stats_.set_ignore_max_ack_delay(true);
  }
  if (config.HasClientSentConnectionOption(kMAD2, perspective)) {
    // Set the minimum to the alarm granularity.
    min_tlp_timeout_ = kAlarmGranularity;
  }
  if (config.HasClientSentConnectionOption(kMAD3, perspective)) {
    // Set the minimum to the alarm granularity.
    min_rto_timeout_ = kAlarmGranularity;
  }

  if (config.HasClientSentConnectionOption(k2PTO, perspective)) {
    pto_enabled_ = true;
  }
  if (config.HasClientSentConnectionOption(k1PTO, perspective)) {
    pto_enabled_ = true;
    max_probe_packets_per_pto_ = 1;
  }

  if (config.HasClientSentConnectionOption(kPTOS, perspective)) {
    if (!pto_enabled_) {
      QUIC_PEER_BUG
          << "PTO is not enabled when receiving PTOS connection option.";
      pto_enabled_ = true;
      max_probe_packets_per_pto_ = 1;
    }
    skip_packet_number_for_pto_ = true;
  }

  if (pto_enabled_) {
    if (config.HasClientSentConnectionOption(kPTOA, perspective)) {
      always_include_max_ack_delay_for_pto_timeout_ = false;
    }
    if (config.HasClientSentConnectionOption(kPEB1, perspective)) {
      StartExponentialBackoffAfterNthPto(1);
    }
    if (config.HasClientSentConnectionOption(kPEB2, perspective)) {
      StartExponentialBackoffAfterNthPto(2);
    }
    if (config.HasClientSentConnectionOption(kPVS1, perspective)) {
      pto_rttvar_multiplier_ = 2;
    }
    if (config.HasClientSentConnectionOption(kPAG1, perspective)) {
      QUIC_CODE_COUNT(one_aggressive_pto);
      num_tlp_timeout_ptos_ = 1;
    }
    if (config.HasClientSentConnectionOption(kPAG2, perspective)) {
      QUIC_CODE_COUNT(two_aggressive_ptos);
      num_tlp_timeout_ptos_ = 2;
    }
    if (config.HasClientSentConnectionOption(kPLE1, perspective) ||
        config.HasClientSentConnectionOption(kTLPR, perspective)) {
      first_pto_srtt_multiplier_ = 0.5;
    } else if (config.HasClientSentConnectionOption(kPLE2, perspective)) {
      first_pto_srtt_multiplier_ = 1.5;
    }
    if (config.HasClientSentConnectionOption(kAPTO, perspective)) {
      pto_multiplier_without_rtt_samples_ = 1.5;
    }
    if (config.HasClientSentConnectionOption(kPSDA, perspective)) {
      use_standard_deviation_for_pto_ = true;
      rtt_stats_.EnableStandardDeviationCalculation();
    }
  }

  // Configure congestion control.
  if (config.HasClientRequestedIndependentOption(kTBBR, perspective)) {
    SetSendAlgorithm(kBBR);
  }
  if (GetQuicReloadableFlag(quic_allow_client_enabled_bbr_v2) &&
      config.HasClientRequestedIndependentOption(kB2ON, perspective)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_allow_client_enabled_bbr_v2);
    SetSendAlgorithm(kBBRv2);
  }

  if (config.HasClientRequestedIndependentOption(kRENO, perspective)) {
    SetSendAlgorithm(kRenoBytes);
  } else if (config.HasClientRequestedIndependentOption(kBYTE, perspective) ||
             (GetQuicReloadableFlag(quic_default_to_bbr) &&
              config.HasClientRequestedIndependentOption(kQBIC, perspective))) {
    SetSendAlgorithm(kCubicBytes);
  }

  // Initial window.
  if (GetQuicReloadableFlag(quic_unified_iw_options)) {
    if (config.HasClientRequestedIndependentOption(kIW03, perspective)) {
      initial_congestion_window_ = 3;
      send_algorithm_->SetInitialCongestionWindowInPackets(3);
    }
    if (config.HasClientRequestedIndependentOption(kIW10, perspective)) {
      initial_congestion_window_ = 10;
      send_algorithm_->SetInitialCongestionWindowInPackets(10);
    }
    if (config.HasClientRequestedIndependentOption(kIW20, perspective)) {
      initial_congestion_window_ = 20;
      send_algorithm_->SetInitialCongestionWindowInPackets(20);
    }
    if (config.HasClientRequestedIndependentOption(kIW50, perspective)) {
      initial_congestion_window_ = 50;
      send_algorithm_->SetInitialCongestionWindowInPackets(50);
    }
  }
  if (config.HasClientRequestedIndependentOption(kBWS5, perspective)) {
    initial_congestion_window_ = 10;
    send_algorithm_->SetInitialCongestionWindowInPackets(10);
  }

  using_pacing_ = !GetQuicFlag(FLAGS_quic_disable_pacing_for_perf_tests);

  if (config.HasClientSentConnectionOption(kNTLP, perspective)) {
    max_tail_loss_probes_ = 0;
  }
  if (config.HasClientSentConnectionOption(k1TLP, perspective)) {
    max_tail_loss_probes_ = 1;
  }
  if (config.HasClientSentConnectionOption(k1RTO, perspective)) {
    max_rto_packets_ = 1;
  }
  if (config.HasClientSentConnectionOption(kTLPR, perspective)) {
    enable_half_rtt_tail_loss_probe_ = true;
  }
  if (config.HasClientRequestedIndependentOption(kTLPR, perspective)) {
    enable_half_rtt_tail_loss_probe_ = true;
  }
  if (config.HasClientSentConnectionOption(kNRTO, perspective)) {
    use_new_rto_ = true;
  }
  // Configure loss detection.
  if (config.HasClientRequestedIndependentOption(kILD0, perspective)) {
    uber_loss_algorithm_.SetReorderingShift(kDefaultIetfLossDelayShift);
    uber_loss_algorithm_.DisableAdaptiveReorderingThreshold();
  }
  if (config.HasClientRequestedIndependentOption(kILD1, perspective)) {
    uber_loss_algorithm_.SetReorderingShift(kDefaultLossDelayShift);
    uber_loss_algorithm_.DisableAdaptiveReorderingThreshold();
  }
  if (config.HasClientRequestedIndependentOption(kILD2, perspective)) {
    uber_loss_algorithm_.EnableAdaptiveReorderingThreshold();
    uber_loss_algorithm_.SetReorderingShift(kDefaultIetfLossDelayShift);
  }
  if (config.HasClientRequestedIndependentOption(kILD3, perspective)) {
    uber_loss_algorithm_.SetReorderingShift(kDefaultLossDelayShift);
    uber_loss_algorithm_.EnableAdaptiveReorderingThreshold();
  }
  if (config.HasClientRequestedIndependentOption(kILD4, perspective)) {
    uber_loss_algorithm_.SetReorderingShift(kDefaultLossDelayShift);
    uber_loss_algorithm_.EnableAdaptiveReorderingThreshold();
    uber_loss_algorithm_.EnableAdaptiveTimeThreshold();
  }
  if (config.HasClientRequestedIndependentOption(kRUNT, perspective)) {
    uber_loss_algorithm_.DisablePacketThresholdForRuntPackets();
  }
  if (config.HasClientSentConnectionOption(kCONH, perspective)) {
    conservative_handshake_retransmits_ = true;
  }
  send_algorithm_->SetFromConfig(config, perspective);
  loss_algorithm_->SetFromConfig(config, perspective);

  if (network_change_visitor_ != nullptr) {
    network_change_visitor_->OnCongestionChange();
  }
}

void QuicSentPacketManager::ApplyConnectionOptions(
    const QuicTagVector& connection_options) {
  send_algorithm_->ApplyConnectionOptions(connection_options);
}

void QuicSentPacketManager::ResumeConnectionState(
    const CachedNetworkParameters& cached_network_params,
    bool max_bandwidth_resumption) {
  QuicBandwidth bandwidth = QuicBandwidth::FromBytesPerSecond(
      max_bandwidth_resumption
          ? cached_network_params.max_bandwidth_estimate_bytes_per_second()
          : cached_network_params.bandwidth_estimate_bytes_per_second());
  QuicTime::Delta rtt =
      QuicTime::Delta::FromMilliseconds(cached_network_params.min_rtt_ms());
  // This calls the old AdjustNetworkParameters interface, and fills certain
  // fields in SendAlgorithmInterface::NetworkParams
  // (e.g., quic_bbr_fix_pacing_rate) using GFE flags.
  AdjustNetworkParameters(SendAlgorithmInterface::NetworkParams(
      bandwidth, rtt, /*allow_cwnd_to_decrease = */ false));
}

void QuicSentPacketManager::AdjustNetworkParameters(
    const SendAlgorithmInterface::NetworkParams& params) {
  const QuicBandwidth& bandwidth = params.bandwidth;
  const QuicTime::Delta& rtt = params.rtt;
  if (!rtt.IsZero()) {
    SetInitialRtt(rtt);
  }
  const QuicByteCount old_cwnd = send_algorithm_->GetCongestionWindow();
  if (GetQuicReloadableFlag(quic_conservative_bursts) && using_pacing_ &&
      !bandwidth.IsZero()) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_conservative_bursts);
    pacing_sender_.SetBurstTokens(kConservativeUnpacedBurst);
  }
  send_algorithm_->AdjustNetworkParameters(params);
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnAdjustNetworkParameters(
        bandwidth, rtt.IsZero() ? rtt_stats_.MinOrInitialRtt() : rtt, old_cwnd,
        send_algorithm_->GetCongestionWindow());
  }
}

void QuicSentPacketManager::SetLossDetectionTuner(
    std::unique_ptr<LossDetectionTunerInterface> tuner) {
  uber_loss_algorithm_.SetLossDetectionTuner(std::move(tuner));
}

void QuicSentPacketManager::OnConfigNegotiated() {
  loss_algorithm_->OnConfigNegotiated();
}

void QuicSentPacketManager::OnConnectionClosed() {
  loss_algorithm_->OnConnectionClosed();
}

void QuicSentPacketManager::SetHandshakeConfirmed() {
  if (!handshake_finished_) {
    handshake_finished_ = true;
    NeuterHandshakePackets();
  }
}

void QuicSentPacketManager::PostProcessNewlyAckedPackets(
    QuicPacketNumber ack_packet_number,
    EncryptionLevel ack_decrypted_level,
    const QuicAckFrame& ack_frame,
    QuicTime ack_receive_time,
    bool rtt_updated,
    QuicByteCount prior_bytes_in_flight) {
  unacked_packets_.NotifyAggregatedStreamFrameAcked(
      last_ack_frame_.ack_delay_time);
  InvokeLossDetection(ack_receive_time);
  // Ignore losses in RTO mode.
  if (consecutive_rto_count_ > 0 && !use_new_rto_) {
    packets_lost_.clear();
  }
  MaybeInvokeCongestionEvent(rtt_updated, prior_bytes_in_flight,
                             ack_receive_time);
  unacked_packets_.RemoveObsoletePackets();

  sustained_bandwidth_recorder_.RecordEstimate(
      send_algorithm_->InRecovery(), send_algorithm_->InSlowStart(),
      send_algorithm_->BandwidthEstimate(), ack_receive_time, clock_->WallNow(),
      rtt_stats_.smoothed_rtt());

  // Anytime we are making forward progress and have a new RTT estimate, reset
  // the backoff counters.
  if (rtt_updated) {
    if (consecutive_rto_count_ > 0) {
      // If the ack acknowledges data sent prior to the RTO,
      // the RTO was spurious.
      if (LargestAcked(ack_frame) < first_rto_transmission_) {
        // Replace SRTT with latest_rtt and increase the variance to prevent
        // a spurious RTO from happening again.
        rtt_stats_.ExpireSmoothedMetrics();
      } else {
        if (!use_new_rto_) {
          send_algorithm_->OnRetransmissionTimeout(true);
        }
      }
    }
    // Records the max consecutive RTO or PTO before forward progress has been
    // made.
    if (consecutive_rto_count_ >
        stats_->max_consecutive_rto_with_forward_progress) {
      stats_->max_consecutive_rto_with_forward_progress =
          consecutive_rto_count_;
    } else if (consecutive_pto_count_ >
               stats_->max_consecutive_rto_with_forward_progress) {
      stats_->max_consecutive_rto_with_forward_progress =
          consecutive_pto_count_;
    }
    // Reset all retransmit counters any time a new packet is acked.
    consecutive_rto_count_ = 0;
    consecutive_tlp_count_ = 0;
    consecutive_pto_count_ = 0;
    consecutive_crypto_retransmission_count_ = 0;
  }

  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnIncomingAck(
        ack_packet_number, ack_decrypted_level, ack_frame, ack_receive_time,
        LargestAcked(ack_frame), rtt_updated, GetLeastUnacked());
  }
  // Remove packets below least unacked from all_packets_acked_ and
  // last_ack_frame_.
  last_ack_frame_.packets.RemoveUpTo(unacked_packets_.GetLeastUnacked());
  last_ack_frame_.received_packet_times.clear();
}

void QuicSentPacketManager::MaybeInvokeCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    QuicTime event_time) {
  if (!rtt_updated && packets_acked_.empty() && packets_lost_.empty()) {
    return;
  }
  const bool overshooting_detected =
      stats_->overshooting_detected_with_network_parameters_adjusted;
  if (using_pacing_) {
    pacing_sender_.OnCongestionEvent(rtt_updated, prior_in_flight, event_time,
                                     packets_acked_, packets_lost_);
  } else {
    send_algorithm_->OnCongestionEvent(rtt_updated, prior_in_flight, event_time,
                                       packets_acked_, packets_lost_);
  }
  if (debug_delegate_ != nullptr && !overshooting_detected &&
      stats_->overshooting_detected_with_network_parameters_adjusted) {
    debug_delegate_->OnOvershootingDetected();
  }
  packets_acked_.clear();
  packets_lost_.clear();
  if (network_change_visitor_ != nullptr) {
    network_change_visitor_->OnCongestionChange();
  }
}

void QuicSentPacketManager::MarkZeroRttPacketsForRetransmission() {
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (it->encryption_level == ENCRYPTION_ZERO_RTT) {
      if (it->in_flight) {
        // Remove 0-RTT packets and packets of the wrong version from flight,
        // because neither can be processed by the peer.
        unacked_packets_.RemoveFromInFlight(&*it);
      }
      if (unacked_packets_.HasRetransmittableFrames(*it)) {
        MarkForRetransmission(packet_number, ALL_ZERO_RTT_RETRANSMISSION);
      }
    }
  }
}

void QuicSentPacketManager::NeuterUnencryptedPackets() {
  for (QuicPacketNumber packet_number :
       unacked_packets_.NeuterUnencryptedPackets()) {
    send_algorithm_->OnPacketNeutered(packet_number);
  }
  if (handshake_mode_disabled_) {
    consecutive_pto_count_ = 0;
    uber_loss_algorithm_.ResetLossDetection(INITIAL_DATA);
  }
}

void QuicSentPacketManager::NeuterHandshakePackets() {
  for (QuicPacketNumber packet_number :
       unacked_packets_.NeuterHandshakePackets()) {
    send_algorithm_->OnPacketNeutered(packet_number);
  }
  if (handshake_mode_disabled_) {
    consecutive_pto_count_ = 0;
    uber_loss_algorithm_.ResetLossDetection(HANDSHAKE_DATA);
  }
}

bool QuicSentPacketManager::ShouldAddMaxAckDelay(
    PacketNumberSpace space) const {
  DCHECK(pto_enabled_);
  if (supports_multiple_packet_number_spaces() && space != APPLICATION_DATA) {
    // When the PTO is armed for Initial or Handshake packet number spaces,
    // the max_ack_delay is 0.
    return false;
  }
  if (always_include_max_ack_delay_for_pto_timeout_) {
    return true;
  }
  if (!unacked_packets_
           .GetLargestSentRetransmittableOfPacketNumberSpace(APPLICATION_DATA)
           .IsInitialized() ||
      unacked_packets_.GetLargestSentRetransmittableOfPacketNumberSpace(
          APPLICATION_DATA) <
          FirstSendingPacketNumber() + kMinReceivedBeforeAckDecimation - 1) {
    // Peer is doing TCP style acking. Expect an immediate ACK if more than 1
    // packet are outstanding.
    if (unacked_packets_.packets_in_flight() >=
        kDefaultRetransmittablePacketsBeforeAck) {
      return false;
    }
  } else if (unacked_packets_.packets_in_flight() >=
             kMaxRetransmittablePacketsBeforeAck) {
    // Peer is doing ack decimation. Expect an immediate ACK if >= 10
    // packets are outstanding.
    return false;
  }
  if (skip_packet_number_for_pto_ && consecutive_pto_count_ > 0) {
    // An immediate ACK is expected when doing PTOS. Please note, this will miss
    // cases when PTO fires and turns out to be spurious.
    return false;
  }
  return true;
}

QuicTime QuicSentPacketManager::GetEarliestPacketSentTimeForPto(
    PacketNumberSpace* packet_number_space) const {
  DCHECK(supports_multiple_packet_number_spaces());
  QuicTime earliest_sent_time = QuicTime::Zero();
  for (int8_t i = 0; i < NUM_PACKET_NUMBER_SPACES; ++i) {
    const QuicTime sent_time = unacked_packets_.GetLastInFlightPacketSentTime(
        static_cast<PacketNumberSpace>(i));
    if (!ShouldArmPtoForApplicationData() && i == APPLICATION_DATA) {
      continue;
    }
    if (!sent_time.IsInitialized() || (earliest_sent_time.IsInitialized() &&
                                       earliest_sent_time <= sent_time)) {
      continue;
    }
    earliest_sent_time = sent_time;
    *packet_number_space = static_cast<PacketNumberSpace>(i);
  }

  return earliest_sent_time;
}

bool QuicSentPacketManager::ShouldArmPtoForApplicationData() const {
  DCHECK(supports_multiple_packet_number_spaces());
  if (GetQuicReloadableFlag(quic_fix_arm_pto_for_application_data)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_fix_arm_pto_for_application_data);
    // Do not arm PTO for application data until handshake gets confirmed.
    return handshake_finished_;
  }
  // Application data must be ignored before handshake completes (1-RTT key
  // is available). Not arming PTO for application data to prioritize the
  // completion of handshake. On the server side, handshake_finished_
  // indicates handshake complete (and confirmed). On the client side,
  // one_rtt_packet_sent_ indicates handshake complete (while handshake
  // confirmation will happen later).
  return handshake_finished_ ||
         (unacked_packets_.perspective() == Perspective::IS_CLIENT &&
          one_rtt_packet_sent_);
}

void QuicSentPacketManager::MarkForRetransmission(
    QuicPacketNumber packet_number,
    TransmissionType transmission_type) {
  QuicTransmissionInfo* transmission_info =
      unacked_packets_.GetMutableTransmissionInfo(packet_number);
  // A previous RTO retransmission may cause connection close; packets without
  // retransmittable frames can be marked for loss retransmissions.
  QUIC_BUG_IF(transmission_type != LOSS_RETRANSMISSION &&
              transmission_type != RTO_RETRANSMISSION &&
              !unacked_packets_.HasRetransmittableFrames(*transmission_info))
      << "transmission_type: " << transmission_type;
  // Handshake packets should never be sent as probing retransmissions.
  DCHECK(!transmission_info->has_crypto_handshake ||
         transmission_type != PROBING_RETRANSMISSION);

  HandleRetransmission(transmission_type, transmission_info);

  // Update packet state according to transmission type.
  transmission_info->state =
      QuicUtils::RetransmissionTypeToPacketState(transmission_type);
}

void QuicSentPacketManager::HandleRetransmission(
    TransmissionType transmission_type,
    QuicTransmissionInfo* transmission_info) {
  if (ShouldForceRetransmission(transmission_type)) {
    // TODO(fayang): Consider to make RTO and PROBING retransmission
    // strategies be configurable by applications. Today, TLP, RTO and PROBING
    // retransmissions are handled similarly, i.e., always retranmist the
    // oldest outstanding data. This is not ideal in general because different
    // applications may want different strategies. For example, some
    // applications may want to use higher priority stream data for bandwidth
    // probing, and some applications want to consider RTO is an indication of
    // loss, etc.
    unacked_packets_.RetransmitFrames(*transmission_info, transmission_type);
    return;
  }

  unacked_packets_.NotifyFramesLost(*transmission_info, transmission_type);
  if (transmission_info->retransmittable_frames.empty()) {
    return;
  }

  if (transmission_type == LOSS_RETRANSMISSION) {
    // Record the first packet sent after loss, which allows to wait 1
    // more RTT before giving up on this lost packet.
    transmission_info->first_sent_after_loss =
        unacked_packets_.largest_sent_packet() + 1;
  } else {
    // Clear the recorded first packet sent after loss when version or
    // encryption changes.
    transmission_info->first_sent_after_loss.Clear();
  }
}

void QuicSentPacketManager::RecordOneSpuriousRetransmission(
    const QuicTransmissionInfo& info) {
  stats_->bytes_spuriously_retransmitted += info.bytes_sent;
  ++stats_->packets_spuriously_retransmitted;
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnSpuriousPacketRetransmission(info.transmission_type,
                                                    info.bytes_sent);
  }
}

void QuicSentPacketManager::MarkPacketHandled(QuicPacketNumber packet_number,
                                              QuicTransmissionInfo* info,
                                              QuicTime ack_receive_time,
                                              QuicTime::Delta ack_delay_time,
                                              QuicTime receive_timestamp) {
  if (info->has_ack_frequency) {
    for (const auto& frame : info->retransmittable_frames) {
      if (frame.type == ACK_FREQUENCY_FRAME) {
        OnAckFrequencyFrameAcked(*frame.ack_frequency_frame);
      }
    }
  }
  // Try to aggregate acked stream frames if acked packet is not a
  // retransmission.
  if (info->transmission_type == NOT_RETRANSMISSION) {
    unacked_packets_.MaybeAggregateAckedStreamFrame(*info, ack_delay_time,
                                                    receive_timestamp);
  } else {
    unacked_packets_.NotifyAggregatedStreamFrameAcked(ack_delay_time);
    const bool new_data_acked = unacked_packets_.NotifyFramesAcked(
        *info, ack_delay_time, receive_timestamp);
    if (!new_data_acked && info->transmission_type != NOT_RETRANSMISSION) {
      // Record as a spurious retransmission if this packet is a
      // retransmission and no new data gets acked.
      QUIC_DVLOG(1) << "Detect spurious retransmitted packet " << packet_number
                    << " transmission type: " << info->transmission_type;
      RecordOneSpuriousRetransmission(*info);
    }
  }
  if (info->state == LOST) {
    // Record as a spurious loss as a packet previously declared lost gets
    // acked.
    const PacketNumberSpace packet_number_space =
        unacked_packets_.GetPacketNumberSpace(info->encryption_level);
    const QuicPacketNumber previous_largest_acked =
        supports_multiple_packet_number_spaces()
            ? unacked_packets_.GetLargestAckedOfPacketNumberSpace(
                  packet_number_space)
            : unacked_packets_.largest_acked();
    QUIC_DVLOG(1) << "Packet " << packet_number
                  << " was detected lost spuriously, "
                     "previous_largest_acked: "
                  << previous_largest_acked;
    loss_algorithm_->SpuriousLossDetected(unacked_packets_, rtt_stats_,
                                          ack_receive_time, packet_number,
                                          previous_largest_acked);
    ++stats_->packet_spuriously_detected_lost;
  }

  if (network_change_visitor_ != nullptr &&
      info->bytes_sent > largest_mtu_acked_) {
    largest_mtu_acked_ = info->bytes_sent;
    network_change_visitor_->OnPathMtuIncreased(largest_mtu_acked_);
  }
  unacked_packets_.RemoveFromInFlight(info);
  unacked_packets_.RemoveRetransmittability(info);
  info->state = ACKED;
}

bool QuicSentPacketManager::OnPacketSent(
    SerializedPacket* mutable_packet,
    QuicTime sent_time,
    TransmissionType transmission_type,
    HasRetransmittableData has_retransmittable_data,
    bool measure_rtt) {
  const SerializedPacket& packet = *mutable_packet;
  QuicPacketNumber packet_number = packet.packet_number;
  DCHECK_LE(FirstSendingPacketNumber(), packet_number);
  DCHECK(!unacked_packets_.IsUnacked(packet_number));
  QUIC_BUG_IF(packet.encrypted_length == 0) << "Cannot send empty packets.";
  if (pending_timer_transmission_count_ > 0) {
    --pending_timer_transmission_count_;
  }

  bool in_flight = has_retransmittable_data == HAS_RETRANSMITTABLE_DATA;
  if (using_pacing_) {
    pacing_sender_.OnPacketSent(sent_time, unacked_packets_.bytes_in_flight(),
                                packet_number, packet.encrypted_length,
                                has_retransmittable_data);
  } else {
    send_algorithm_->OnPacketSent(sent_time, unacked_packets_.bytes_in_flight(),
                                  packet_number, packet.encrypted_length,
                                  has_retransmittable_data);
  }

  if (packet.encryption_level == ENCRYPTION_FORWARD_SECURE) {
    one_rtt_packet_sent_ = true;
  }

  if (GetQuicReloadableFlag(quic_deallocate_message_right_after_sent)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_deallocate_message_right_after_sent);
    // Deallocate message data in QuicMessageFrame immediately after packet
    // sent.
    if (packet.has_message) {
      for (auto& frame : mutable_packet->retransmittable_frames) {
        if (frame.type == MESSAGE_FRAME) {
          frame.message_frame->message_data.clear();
          frame.message_frame->message_length = 0;
        }
      }
    }
  }

  if (packet.has_ack_frequency) {
    for (const auto& frame : packet.retransmittable_frames) {
      if (frame.type == ACK_FREQUENCY_FRAME) {
        OnAckFrequencyFrameSent(*frame.ack_frequency_frame);
      }
    }
  }
  unacked_packets_.AddSentPacket(mutable_packet, transmission_type, sent_time,
                                 in_flight, measure_rtt);
  // Reset the retransmission timer anytime a pending packet is sent.
  return in_flight;
}

QuicSentPacketManager::RetransmissionTimeoutMode
QuicSentPacketManager::OnRetransmissionTimeout() {
  DCHECK(unacked_packets_.HasInFlightPackets() ||
         (handshake_mode_disabled_ && !handshake_finished_));
  DCHECK_EQ(0u, pending_timer_transmission_count_);
  // Handshake retransmission, timer based loss detection, TLP, and RTO are
  // implemented with a single alarm. The handshake alarm is set when the
  // handshake has not completed, the loss alarm is set when the loss detection
  // algorithm says to, and the TLP and  RTO alarms are set after that.
  // The TLP alarm is always set to run for under an RTO.
  switch (GetRetransmissionMode()) {
    case HANDSHAKE_MODE:
      DCHECK(!handshake_mode_disabled_);
      ++stats_->crypto_retransmit_count;
      RetransmitCryptoPackets();
      return HANDSHAKE_MODE;
    case LOSS_MODE: {
      ++stats_->loss_timeout_count;
      QuicByteCount prior_in_flight = unacked_packets_.bytes_in_flight();
      const QuicTime now = clock_->Now();
      InvokeLossDetection(now);
      MaybeInvokeCongestionEvent(false, prior_in_flight, now);
      return LOSS_MODE;
    }
    case TLP_MODE:
      ++stats_->tlp_count;
      ++consecutive_tlp_count_;
      pending_timer_transmission_count_ = 1;
      // TLPs prefer sending new data instead of retransmitting data, so
      // give the connection a chance to write before completing the TLP.
      return TLP_MODE;
    case RTO_MODE:
      ++stats_->rto_count;
      RetransmitRtoPackets();
      return RTO_MODE;
    case PTO_MODE:
      QUIC_DVLOG(1) << ENDPOINT << "PTO mode";
      ++stats_->pto_count;
      if (handshake_mode_disabled_ && !ShouldArmPtoForApplicationData()) {
        ++stats_->crypto_retransmit_count;
      }
      ++consecutive_pto_count_;
      pending_timer_transmission_count_ = max_probe_packets_per_pto_;
      return PTO_MODE;
  }
  QUIC_BUG << "Unknown retransmission mode " << GetRetransmissionMode();
  return GetRetransmissionMode();
}

void QuicSentPacketManager::RetransmitCryptoPackets() {
  DCHECK_EQ(HANDSHAKE_MODE, GetRetransmissionMode());
  ++consecutive_crypto_retransmission_count_;
  bool packet_retransmitted = false;
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  std::vector<QuicPacketNumber> crypto_retransmissions;
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    // Only retransmit frames which are in flight, and therefore have been sent.
    if (!it->in_flight || it->state != OUTSTANDING ||
        !it->has_crypto_handshake ||
        !unacked_packets_.HasRetransmittableFrames(*it)) {
      continue;
    }
    packet_retransmitted = true;
    crypto_retransmissions.push_back(packet_number);
    ++pending_timer_transmission_count_;
  }
  DCHECK(packet_retransmitted) << "No crypto packets found to retransmit.";
  for (QuicPacketNumber retransmission : crypto_retransmissions) {
    MarkForRetransmission(retransmission, HANDSHAKE_RETRANSMISSION);
  }
}

bool QuicSentPacketManager::MaybeRetransmitTailLossProbe() {
  DCHECK(!pto_enabled_);
  if (pending_timer_transmission_count_ == 0) {
    return false;
  }
  if (!MaybeRetransmitOldestPacket(TLP_RETRANSMISSION)) {
    return false;
  }
  return true;
}

bool QuicSentPacketManager::MaybeRetransmitOldestPacket(TransmissionType type) {
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    // Only retransmit frames which are in flight, and therefore have been sent.
    if (!it->in_flight || it->state != OUTSTANDING ||
        !unacked_packets_.HasRetransmittableFrames(*it)) {
      continue;
    }
    MarkForRetransmission(packet_number, type);
    return true;
  }
  QUIC_DVLOG(1)
      << "No retransmittable packets, so RetransmitOldestPacket failed.";
  return false;
}

void QuicSentPacketManager::RetransmitRtoPackets() {
  DCHECK(!pto_enabled_);
  QUIC_BUG_IF(pending_timer_transmission_count_ > 0)
      << "Retransmissions already queued:" << pending_timer_transmission_count_;
  // Mark two packets for retransmission.
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  std::vector<QuicPacketNumber> retransmissions;
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (it->state == OUTSTANDING &&
        unacked_packets_.HasRetransmittableFrames(*it) &&
        pending_timer_transmission_count_ < max_rto_packets_) {
      DCHECK(it->in_flight);
      retransmissions.push_back(packet_number);
      ++pending_timer_transmission_count_;
    }
  }
  if (pending_timer_transmission_count_ > 0) {
    if (consecutive_rto_count_ == 0) {
      first_rto_transmission_ = unacked_packets_.largest_sent_packet() + 1;
    }
    ++consecutive_rto_count_;
  }
  for (QuicPacketNumber retransmission : retransmissions) {
    MarkForRetransmission(retransmission, RTO_RETRANSMISSION);
  }
  if (retransmissions.empty()) {
    QUIC_BUG_IF(pending_timer_transmission_count_ != 0);
    // No packets to be RTO retransmitted, raise up a credit to allow
    // connection to send.
    QUIC_CODE_COUNT(no_packets_to_be_rto_retransmitted);
    pending_timer_transmission_count_ = 1;
  }
}

void QuicSentPacketManager::MaybeSendProbePackets() {
  if (pending_timer_transmission_count_ == 0) {
    return;
  }
  PacketNumberSpace packet_number_space;
  if (supports_multiple_packet_number_spaces()) {
    // Find out the packet number space to send probe packets.
    if (!GetEarliestPacketSentTimeForPto(&packet_number_space)
             .IsInitialized()) {
      QUIC_BUG_IF(unacked_packets_.perspective() == Perspective::IS_SERVER)
          << "earlist_sent_time not initialized when trying to send PTO "
             "retransmissions";
      return;
    }
  }
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  std::vector<QuicPacketNumber> probing_packets;
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (it->state == OUTSTANDING &&
        unacked_packets_.HasRetransmittableFrames(*it) &&
        (!supports_multiple_packet_number_spaces() ||
         unacked_packets_.GetPacketNumberSpace(it->encryption_level) ==
             packet_number_space)) {
      DCHECK(it->in_flight);
      probing_packets.push_back(packet_number);
      if (probing_packets.size() == pending_timer_transmission_count_) {
        break;
      }
    }
  }

  for (QuicPacketNumber retransmission : probing_packets) {
    QUIC_DVLOG(1) << ENDPOINT << "Marking " << retransmission
                  << " for probing retransmission";
    MarkForRetransmission(retransmission, PTO_RETRANSMISSION);
  }
  // It is possible that there is not enough outstanding data for probing.
}

void QuicSentPacketManager::AdjustPendingTimerTransmissions() {
  if (pending_timer_transmission_count_ < max_probe_packets_per_pto_) {
    // There are packets sent already, clear credit.
    pending_timer_transmission_count_ = 0;
    return;
  }
  // No packet gets sent, leave 1 credit to allow data to be write eventually.
  pending_timer_transmission_count_ = 1;
}

void QuicSentPacketManager::EnableIetfPtoAndLossDetection() {
  if (pto_enabled_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_default_on_pto, 2, 2);
    // Disable handshake mode.
    handshake_mode_disabled_ = true;
    return;
  }
  pto_enabled_ = true;
  handshake_mode_disabled_ = true;
  // Default to 1 packet per PTO and skip a packet number. Arm the 1st PTO with
  // max of earliest in flight sent time + PTO delay and 1.5 * srtt from
  // last in flight packet.
  max_probe_packets_per_pto_ = 1;
  skip_packet_number_for_pto_ = true;
  first_pto_srtt_multiplier_ = 1.5;
  if (GetQuicReloadableFlag(quic_default_to_2_rttvar)) {
    QUIC_RELOADABLE_FLAG_COUNT(quic_default_to_2_rttvar);
    pto_rttvar_multiplier_ = 2;
  }
}

void QuicSentPacketManager::StartExponentialBackoffAfterNthPto(
    size_t exponential_backoff_start_point) {
  pto_exponential_backoff_start_point_ = exponential_backoff_start_point;
}

void QuicSentPacketManager::RetransmitDataOfSpaceIfAny(
    PacketNumberSpace space) {
  DCHECK(supports_multiple_packet_number_spaces());
  if (!unacked_packets_.GetLastInFlightPacketSentTime(space).IsInitialized()) {
    // No in flight data of space.
    return;
  }
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (it->state == OUTSTANDING &&
        unacked_packets_.HasRetransmittableFrames(*it) &&
        unacked_packets_.GetPacketNumberSpace(it->encryption_level) == space) {
      DCHECK(it->in_flight);
      if (GetQuicReloadableFlag(quic_fix_pto_pending_timer_count) &&
          pending_timer_transmission_count_ == 0) {
        QUIC_RELOADABLE_FLAG_COUNT(quic_fix_pto_pending_timer_count);
        pending_timer_transmission_count_ = 1;
      }
      MarkForRetransmission(packet_number, PTO_RETRANSMISSION);
      return;
    }
  }
}

QuicSentPacketManager::RetransmissionTimeoutMode
QuicSentPacketManager::GetRetransmissionMode() const {
  DCHECK(unacked_packets_.HasInFlightPackets() ||
         (handshake_mode_disabled_ && !handshake_finished_));
  if (!handshake_mode_disabled_ && !handshake_finished_ &&
      unacked_packets_.HasPendingCryptoPackets()) {
    return HANDSHAKE_MODE;
  }
  if (loss_algorithm_->GetLossTimeout() != QuicTime::Zero()) {
    return LOSS_MODE;
  }
  if (pto_enabled_) {
    return PTO_MODE;
  }
  if (consecutive_tlp_count_ < max_tail_loss_probes_) {
    if (unacked_packets_.HasUnackedRetransmittableFrames()) {
      return TLP_MODE;
    }
  }
  return RTO_MODE;
}

void QuicSentPacketManager::InvokeLossDetection(QuicTime time) {
  if (!packets_acked_.empty()) {
    DCHECK_LE(packets_acked_.front().packet_number,
              packets_acked_.back().packet_number);
    largest_newly_acked_ = packets_acked_.back().packet_number;
  }
  LossDetectionInterface::DetectionStats detection_stats =
      loss_algorithm_->DetectLosses(unacked_packets_, time, rtt_stats_,
                                    largest_newly_acked_, packets_acked_,
                                    &packets_lost_);

  if (detection_stats.sent_packets_max_sequence_reordering >
      stats_->sent_packets_max_sequence_reordering) {
    stats_->sent_packets_max_sequence_reordering =
        detection_stats.sent_packets_max_sequence_reordering;
  }

  stats_->sent_packets_num_borderline_time_reorderings +=
      detection_stats.sent_packets_num_borderline_time_reorderings;

  stats_->total_loss_detection_response_time +=
      detection_stats.total_loss_detection_response_time;

  for (const LostPacket& packet : packets_lost_) {
    QuicTransmissionInfo* info =
        unacked_packets_.GetMutableTransmissionInfo(packet.packet_number);
    ++stats_->packets_lost;
    if (debug_delegate_ != nullptr) {
      debug_delegate_->OnPacketLoss(packet.packet_number,
                                    info->encryption_level, LOSS_RETRANSMISSION,
                                    time);
    }
    unacked_packets_.RemoveFromInFlight(info);

    MarkForRetransmission(packet.packet_number, LOSS_RETRANSMISSION);
  }
}

bool QuicSentPacketManager::MaybeUpdateRTT(QuicPacketNumber largest_acked,
                                           QuicTime::Delta ack_delay_time,
                                           QuicTime ack_receive_time) {
  // We rely on ack_delay_time to compute an RTT estimate, so we
  // only update rtt when the largest observed gets acked.
  if (!unacked_packets_.IsUnacked(largest_acked)) {
    return false;
  }
  // We calculate the RTT based on the highest ACKed packet number, the lower
  // packet numbers will include the ACK aggregation delay.
  const QuicTransmissionInfo& transmission_info =
      unacked_packets_.GetTransmissionInfo(largest_acked);
  // Ensure the packet has a valid sent time.
  if (transmission_info.sent_time == QuicTime::Zero()) {
    QUIC_BUG << "Acked packet has zero sent time, largest_acked:"
             << largest_acked;
    return false;
  }
  if (transmission_info.sent_time > ack_receive_time) {
    QUIC_CODE_COUNT(quic_receive_acked_before_sending);
  }

  QuicTime::Delta send_delta = ack_receive_time - transmission_info.sent_time;
  const bool min_rtt_available = !rtt_stats_.min_rtt().IsZero();
  rtt_stats_.UpdateRtt(send_delta, ack_delay_time, ack_receive_time);

  if (!min_rtt_available && !rtt_stats_.min_rtt().IsZero()) {
    loss_algorithm_->OnMinRttAvailable();
  }

  return true;
}

QuicTime::Delta QuicSentPacketManager::TimeUntilSend(QuicTime now) const {
  // The TLP logic is entirely contained within QuicSentPacketManager, so the
  // send algorithm does not need to be consulted.
  if (pending_timer_transmission_count_ > 0) {
    return QuicTime::Delta::Zero();
  }

  if (using_pacing_) {
    return pacing_sender_.TimeUntilSend(now,
                                        unacked_packets_.bytes_in_flight());
  }

  return send_algorithm_->CanSend(unacked_packets_.bytes_in_flight())
             ? QuicTime::Delta::Zero()
             : QuicTime::Delta::Infinite();
}

const QuicTime QuicSentPacketManager::GetRetransmissionTime() const {
  if (!unacked_packets_.HasInFlightPackets() &&
      PeerCompletedAddressValidation()) {
    return QuicTime::Zero();
  }
  if (pending_timer_transmission_count_ > 0) {
    // Do not set the timer if there is any credit left.
    return QuicTime::Zero();
  }
  PacketNumberSpace packet_number_space;
  if (supports_multiple_packet_number_spaces() &&
      unacked_packets_.perspective() == Perspective::IS_SERVER &&
      !GetEarliestPacketSentTimeForPto(&packet_number_space).IsInitialized()) {
    // Do not set the timer on the server side if the only in flight packets are
    // half RTT data.
    return QuicTime::Zero();
  }
  switch (GetRetransmissionMode()) {
    case HANDSHAKE_MODE:
      return unacked_packets_.GetLastCryptoPacketSentTime() +
             GetCryptoRetransmissionDelay();
    case LOSS_MODE:
      return loss_algorithm_->GetLossTimeout();
    case TLP_MODE: {
      DCHECK(!pto_enabled_);
      // TODO(ianswett): When CWND is available, it would be preferable to
      // set the timer based on the earliest retransmittable packet.
      // Base the updated timer on the send time of the last packet.
      const QuicTime sent_time =
          unacked_packets_.GetLastInFlightPacketSentTime();
      const QuicTime tlp_time = sent_time + GetTailLossProbeDelay();
      // Ensure the TLP timer never gets set to a time in the past.
      return std::max(clock_->ApproximateNow(), tlp_time);
    }
    case RTO_MODE: {
      DCHECK(!pto_enabled_);
      // The RTO is based on the first outstanding packet.
      const QuicTime sent_time =
          unacked_packets_.GetLastInFlightPacketSentTime();
      QuicTime rto_time = sent_time + GetRetransmissionDelay();
      // Wait for TLP packets to be acked before an RTO fires.
      QuicTime tlp_time = sent_time + GetTailLossProbeDelay();
      return std::max(tlp_time, rto_time);
    }
    case PTO_MODE: {
      if (!supports_multiple_packet_number_spaces()) {
        if (first_pto_srtt_multiplier_ > 0 &&
            unacked_packets_.HasInFlightPackets() &&
            consecutive_pto_count_ == 0) {
          // Arm 1st PTO with earliest in flight sent time, and make sure at
          // least first_pto_srtt_multiplier_ * RTT has been passed since last
          // in flight packet.
          return std::max(
              clock_->ApproximateNow(),
              std::max(unacked_packets_.GetFirstInFlightTransmissionInfo()
                               ->sent_time +
                           GetProbeTimeoutDelay(NUM_PACKET_NUMBER_SPACES),
                       unacked_packets_.GetLastInFlightPacketSentTime() +
                           first_pto_srtt_multiplier_ *
                               rtt_stats_.SmoothedOrInitialRtt()));
        }
        // Ensure PTO never gets set to a time in the past.
        return std::max(clock_->ApproximateNow(),
                        unacked_packets_.GetLastInFlightPacketSentTime() +
                            GetProbeTimeoutDelay(NUM_PACKET_NUMBER_SPACES));
      }

      PacketNumberSpace packet_number_space = NUM_PACKET_NUMBER_SPACES;
      // earliest_right_edge is the earliest sent time of the last in flight
      // packet of all packet number spaces.
      QuicTime earliest_right_edge =
          GetEarliestPacketSentTimeForPto(&packet_number_space);
      if (!earliest_right_edge.IsInitialized()) {
        // Arm PTO from now if there is no in flight packets.
        earliest_right_edge = clock_->ApproximateNow();
      }
      if (first_pto_srtt_multiplier_ > 0 &&
          packet_number_space == APPLICATION_DATA &&
          consecutive_pto_count_ == 0) {
        const QuicTransmissionInfo* first_application_info =
            unacked_packets_.GetFirstInFlightTransmissionInfoOfSpace(
                APPLICATION_DATA);
        if (first_application_info != nullptr) {
          // Arm 1st PTO with earliest in flight sent time, and make sure at
          // least first_pto_srtt_multiplier_ * RTT has been passed since last
          // in flight packet. Only do this for application data.
          return std::max(
              clock_->ApproximateNow(),
              std::max(
                  first_application_info->sent_time +
                      GetProbeTimeoutDelay(packet_number_space),
                  earliest_right_edge + first_pto_srtt_multiplier_ *
                                            rtt_stats_.SmoothedOrInitialRtt()));
        }
      }
      return std::max(
          clock_->ApproximateNow(),
          earliest_right_edge + GetProbeTimeoutDelay(packet_number_space));
    }
  }
  DCHECK(false);
  return QuicTime::Zero();
}

const QuicTime::Delta QuicSentPacketManager::GetPathDegradingDelay() const {
  return GetNConsecutiveRetransmissionTimeoutDelay(
      max_tail_loss_probes_ + kNumRetransmissionDelaysForPathDegradingDelay);
}

const QuicTime::Delta QuicSentPacketManager::GetNetworkBlackholeDelay(
    int8_t num_rtos_for_blackhole_detection) const {
  return GetNConsecutiveRetransmissionTimeoutDelay(
      max_tail_loss_probes_ + num_rtos_for_blackhole_detection);
}

QuicTime::Delta QuicSentPacketManager::GetMtuReductionDelay(
    int8_t num_rtos_for_blackhole_detection) const {
  return GetNetworkBlackholeDelay(num_rtos_for_blackhole_detection / 2);
}

const QuicTime::Delta QuicSentPacketManager::GetCryptoRetransmissionDelay()
    const {
  // This is equivalent to the TailLossProbeDelay, but slightly more aggressive
  // because crypto handshake messages don't incur a delayed ack time.
  QuicTime::Delta srtt = rtt_stats_.SmoothedOrInitialRtt();
  int64_t delay_ms;
  if (conservative_handshake_retransmits_) {
    // Using the delayed ack time directly could cause conservative handshake
    // retransmissions to actually be more aggressive than the default.
    delay_ms = std::max(peer_max_ack_delay_.ToMilliseconds(),
                        static_cast<int64_t>(2 * srtt.ToMilliseconds()));
  } else {
    delay_ms = std::max(kMinHandshakeTimeoutMs,
                        static_cast<int64_t>(1.5 * srtt.ToMilliseconds()));
  }
  return QuicTime::Delta::FromMilliseconds(
      delay_ms << consecutive_crypto_retransmission_count_);
}

const QuicTime::Delta QuicSentPacketManager::GetTailLossProbeDelay() const {
  QuicTime::Delta srtt = rtt_stats_.SmoothedOrInitialRtt();
  if (enable_half_rtt_tail_loss_probe_ && consecutive_tlp_count_ == 0u) {
    if (unacked_packets().HasUnackedStreamData()) {
      // Enable TLPR if there are pending data packets.
      return std::max(min_tlp_timeout_, srtt * 0.5);
    }
  }
  if (!unacked_packets_.HasMultipleInFlightPackets()) {
    // This expression really should be using the delayed ack time, but in TCP
    // MinRTO was traditionally set to 2x the delayed ack timer and this
    // expression assumed QUIC did the same.
    return std::max(2 * srtt, 1.5 * srtt + (min_rto_timeout_ * 0.5));
  }
  return std::max(min_tlp_timeout_, 2 * srtt);
}

const QuicTime::Delta QuicSentPacketManager::GetRetransmissionDelay() const {
  QuicTime::Delta retransmission_delay = QuicTime::Delta::Zero();
  if (rtt_stats_.smoothed_rtt().IsZero()) {
    // We are in the initial state, use default timeout values.
    retransmission_delay =
        QuicTime::Delta::FromMilliseconds(kDefaultRetransmissionTimeMs);
  } else {
    retransmission_delay =
        rtt_stats_.smoothed_rtt() + 4 * rtt_stats_.mean_deviation();
    if (retransmission_delay < min_rto_timeout_) {
      retransmission_delay = min_rto_timeout_;
    }
  }

  // Calculate exponential back off.
  retransmission_delay =
      retransmission_delay *
      (1 << std::min<size_t>(consecutive_rto_count_, kMaxRetransmissions));

  if (retransmission_delay.ToMilliseconds() > kMaxRetransmissionTimeMs) {
    return QuicTime::Delta::FromMilliseconds(kMaxRetransmissionTimeMs);
  }
  return retransmission_delay;
}

const QuicTime::Delta QuicSentPacketManager::GetProbeTimeoutDelay(
    PacketNumberSpace space) const {
  DCHECK(pto_enabled_);
  if (rtt_stats_.smoothed_rtt().IsZero()) {
    // Respect kMinHandshakeTimeoutMs to avoid a potential amplification attack.
    QUIC_BUG_IF(rtt_stats_.initial_rtt().IsZero());
    return std::max(
               pto_multiplier_without_rtt_samples_ * rtt_stats_.initial_rtt(),
               QuicTime::Delta::FromMilliseconds(kMinHandshakeTimeoutMs)) *
           (1 << consecutive_pto_count_);
  }
  if (enable_half_rtt_tail_loss_probe_ && consecutive_pto_count_ == 0 &&
      handshake_finished_) {
    return std::max(min_tlp_timeout_, rtt_stats_.smoothed_rtt() * 0.5);
  }
  const QuicTime::Delta rtt_var = use_standard_deviation_for_pto_
                                      ? rtt_stats_.GetStandardOrMeanDeviation()
                                      : rtt_stats_.mean_deviation();
  QuicTime::Delta pto_delay =
      rtt_stats_.smoothed_rtt() +
      std::max(pto_rttvar_multiplier_ * rtt_var, kAlarmGranularity) +
      (ShouldAddMaxAckDelay(space) ? peer_max_ack_delay_
                                   : QuicTime::Delta::Zero());
  pto_delay =
      pto_delay * (1 << (consecutive_pto_count_ -
                         std::min(consecutive_pto_count_,
                                  pto_exponential_backoff_start_point_)));
  if (consecutive_pto_count_ < num_tlp_timeout_ptos_) {
    // Make first n PTOs similar to TLPs.
    if (pto_delay > 2 * rtt_stats_.smoothed_rtt()) {
      QUIC_CODE_COUNT(quic_delayed_pto);
      pto_delay = std::max(kAlarmGranularity, 2 * rtt_stats_.smoothed_rtt());
    } else {
      QUIC_CODE_COUNT(quic_faster_pto);
    }
  }
  return pto_delay;
}

QuicTime::Delta QuicSentPacketManager::GetSlowStartDuration() const {
  if (send_algorithm_->GetCongestionControlType() == kBBR ||
      send_algorithm_->GetCongestionControlType() == kBBRv2) {
    return stats_->slowstart_duration.GetTotalElapsedTime(
        clock_->ApproximateNow());
  }
  return QuicTime::Delta::Infinite();
}

std::string QuicSentPacketManager::GetDebugState() const {
  return send_algorithm_->GetDebugState();
}

void QuicSentPacketManager::SetSendAlgorithm(
    CongestionControlType congestion_control_type) {
  if (send_algorithm_ &&
      send_algorithm_->GetCongestionControlType() == congestion_control_type) {
    return;
  }

  SetSendAlgorithm(SendAlgorithmInterface::Create(
      clock_, &rtt_stats_, &unacked_packets_, congestion_control_type, random_,
      stats_, initial_congestion_window_, send_algorithm_.get()));
}

void QuicSentPacketManager::SetSendAlgorithm(
    SendAlgorithmInterface* send_algorithm) {
  send_algorithm_.reset(send_algorithm);
  pacing_sender_.set_sender(send_algorithm);
}

void QuicSentPacketManager::OnConnectionMigration(AddressChangeType type) {
  if (type == PORT_CHANGE || type == IPV4_SUBNET_CHANGE) {
    // Rtt and cwnd do not need to be reset when the peer address change is
    // considered to be caused by NATs.
    return;
  }
  consecutive_rto_count_ = 0;
  consecutive_tlp_count_ = 0;
  consecutive_pto_count_ = 0;
  rtt_stats_.OnConnectionMigration();
  send_algorithm_->OnConnectionMigration();
}

void QuicSentPacketManager::OnAckFrameStart(QuicPacketNumber largest_acked,
                                            QuicTime::Delta ack_delay_time,
                                            QuicTime ack_receive_time) {
  DCHECK(packets_acked_.empty());
  DCHECK_LE(largest_acked, unacked_packets_.largest_sent_packet());
  if (ack_delay_time > peer_max_ack_delay()) {
    ack_delay_time = peer_max_ack_delay();
  }
  rtt_updated_ =
      MaybeUpdateRTT(largest_acked, ack_delay_time, ack_receive_time);
  last_ack_frame_.ack_delay_time = ack_delay_time;
  acked_packets_iter_ = last_ack_frame_.packets.rbegin();
}

void QuicSentPacketManager::OnAckRange(QuicPacketNumber start,
                                       QuicPacketNumber end) {
  if (!last_ack_frame_.largest_acked.IsInitialized() ||
      end > last_ack_frame_.largest_acked + 1) {
    // Largest acked increases.
    unacked_packets_.IncreaseLargestAcked(end - 1);
    last_ack_frame_.largest_acked = end - 1;
  }
  // Drop ack ranges which ack packets below least_unacked.
  QuicPacketNumber least_unacked = unacked_packets_.GetLeastUnacked();
  if (least_unacked.IsInitialized() && end <= least_unacked) {
    return;
  }
  start = std::max(start, least_unacked);
  do {
    QuicPacketNumber newly_acked_start = start;
    if (acked_packets_iter_ != last_ack_frame_.packets.rend()) {
      newly_acked_start = std::max(start, acked_packets_iter_->max());
    }
    for (QuicPacketNumber acked = end - 1; acked >= newly_acked_start;
         --acked) {
      // Check if end is above the current range. If so add newly acked packets
      // in descending order.
      packets_acked_.push_back(AckedPacket(acked, 0, QuicTime::Zero()));
      if (acked == FirstSendingPacketNumber()) {
        break;
      }
    }
    if (acked_packets_iter_ == last_ack_frame_.packets.rend() ||
        start > acked_packets_iter_->min()) {
      // Finish adding all newly acked packets.
      return;
    }
    end = std::min(end, acked_packets_iter_->min());
    ++acked_packets_iter_;
  } while (start < end);
}

void QuicSentPacketManager::OnAckTimestamp(QuicPacketNumber packet_number,
                                           QuicTime timestamp) {
  last_ack_frame_.received_packet_times.push_back({packet_number, timestamp});
  for (AckedPacket& packet : packets_acked_) {
    if (packet.packet_number == packet_number) {
      packet.receive_timestamp = timestamp;
      return;
    }
  }
}

AckResult QuicSentPacketManager::OnAckFrameEnd(
    QuicTime ack_receive_time,
    QuicPacketNumber ack_packet_number,
    EncryptionLevel ack_decrypted_level) {
  QuicByteCount prior_bytes_in_flight = unacked_packets_.bytes_in_flight();
  // Reverse packets_acked_ so that it is in ascending order.
  std::reverse(packets_acked_.begin(), packets_acked_.end());
  for (AckedPacket& acked_packet : packets_acked_) {
    QuicTransmissionInfo* info =
        unacked_packets_.GetMutableTransmissionInfo(acked_packet.packet_number);
    if (!QuicUtils::IsAckable(info->state)) {
      if (info->state == ACKED) {
        QUIC_BUG << "Trying to ack an already acked packet: "
                 << acked_packet.packet_number
                 << ", last_ack_frame_: " << last_ack_frame_
                 << ", least_unacked: " << unacked_packets_.GetLeastUnacked()
                 << ", packets_acked_: " << packets_acked_;
      } else {
        QUIC_PEER_BUG << "Received " << ack_decrypted_level
                      << " ack for unackable packet: "
                      << acked_packet.packet_number << " with state: "
                      << QuicUtils::SentPacketStateToString(info->state);
        if (supports_multiple_packet_number_spaces()) {
          if (info->state == NEVER_SENT) {
            return UNSENT_PACKETS_ACKED;
          }
          return UNACKABLE_PACKETS_ACKED;
        }
      }
      continue;
    }
    QUIC_DVLOG(1) << ENDPOINT << "Got an " << ack_decrypted_level
                  << " ack for packet " << acked_packet.packet_number
                  << " , state: "
                  << QuicUtils::SentPacketStateToString(info->state);
    const PacketNumberSpace packet_number_space =
        unacked_packets_.GetPacketNumberSpace(info->encryption_level);
    if (supports_multiple_packet_number_spaces() &&
        QuicUtils::GetPacketNumberSpace(ack_decrypted_level) !=
            packet_number_space) {
      return PACKETS_ACKED_IN_WRONG_PACKET_NUMBER_SPACE;
    }
    last_ack_frame_.packets.Add(acked_packet.packet_number);
    if (info->encryption_level == ENCRYPTION_HANDSHAKE) {
      handshake_packet_acked_ = true;
    } else if (info->encryption_level == ENCRYPTION_ZERO_RTT) {
      zero_rtt_packet_acked_ = true;
    } else if (info->encryption_level == ENCRYPTION_FORWARD_SECURE) {
      one_rtt_packet_acked_ = true;
    }
    largest_packet_peer_knows_is_acked_.UpdateMax(info->largest_acked);
    if (supports_multiple_packet_number_spaces()) {
      largest_packets_peer_knows_is_acked_[packet_number_space].UpdateMax(
          info->largest_acked);
    }
    // If data is associated with the most recent transmission of this
    // packet, then inform the caller.
    if (info->in_flight) {
      acked_packet.bytes_acked = info->bytes_sent;
    } else {
      // Unackable packets are skipped earlier.
      largest_newly_acked_ = acked_packet.packet_number;
    }
    unacked_packets_.MaybeUpdateLargestAckedOfPacketNumberSpace(
        packet_number_space, acked_packet.packet_number);
    MarkPacketHandled(acked_packet.packet_number, info, ack_receive_time,
                      last_ack_frame_.ack_delay_time,
                      acked_packet.receive_timestamp);
  }
  const bool acked_new_packet = !packets_acked_.empty();
  PostProcessNewlyAckedPackets(ack_packet_number, ack_decrypted_level,
                               last_ack_frame_, ack_receive_time, rtt_updated_,
                               prior_bytes_in_flight);

  return acked_new_packet ? PACKETS_NEWLY_ACKED : NO_PACKETS_NEWLY_ACKED;
}

void QuicSentPacketManager::SetDebugDelegate(DebugDelegate* debug_delegate) {
  debug_delegate_ = debug_delegate;
}

void QuicSentPacketManager::OnApplicationLimited() {
  if (using_pacing_) {
    pacing_sender_.OnApplicationLimited();
  }
  send_algorithm_->OnApplicationLimited(unacked_packets_.bytes_in_flight());
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnApplicationLimited();
  }
}

NextReleaseTimeResult QuicSentPacketManager::GetNextReleaseTime() const {
  if (!using_pacing_) {
    return {QuicTime::Zero(), false};
  }

  return pacing_sender_.GetNextReleaseTime();
}

void QuicSentPacketManager::SetInitialRtt(QuicTime::Delta rtt) {
  const QuicTime::Delta min_rtt =
      QuicTime::Delta::FromMicroseconds(kMinInitialRoundTripTimeUs);
  QuicTime::Delta max_rtt =
      QuicTime::Delta::FromMicroseconds(kMaxInitialRoundTripTimeUs);
  if (GetQuicReloadableFlag(quic_cap_large_client_initial_rtt)) {
    // TODO(fayang): change the value of kMaxInitialRoundTripTimeUs when
    // deprecating quic_cap_large_client_initial_rtt.
    max_rtt = QuicTime::Delta::FromSeconds(1);
  }
  rtt_stats_.set_initial_rtt(std::max(min_rtt, std::min(max_rtt, rtt)));
}

void QuicSentPacketManager::EnableMultiplePacketNumberSpacesSupport() {
  EnableIetfPtoAndLossDetection();
  unacked_packets_.EnableMultiplePacketNumberSpacesSupport();
}

QuicPacketNumber QuicSentPacketManager::GetLargestAckedPacket(
    EncryptionLevel decrypted_packet_level) const {
  DCHECK(supports_multiple_packet_number_spaces());
  return unacked_packets_.GetLargestAckedOfPacketNumberSpace(
      QuicUtils::GetPacketNumberSpace(decrypted_packet_level));
}

QuicPacketNumber QuicSentPacketManager::GetLeastPacketAwaitedByPeer(
    EncryptionLevel encryption_level) const {
  QuicPacketNumber largest_acked;
  if (supports_multiple_packet_number_spaces()) {
    largest_acked = GetLargestAckedPacket(encryption_level);
  } else {
    largest_acked = GetLargestObserved();
  }
  if (!largest_acked.IsInitialized()) {
    // If no packets have been acked, return the first sent packet to ensure
    // we use a large enough packet number length.
    return FirstSendingPacketNumber();
  }
  QuicPacketNumber least_awaited = largest_acked + 1;
  QuicPacketNumber least_unacked = GetLeastUnacked();
  if (least_unacked.IsInitialized() && least_unacked < least_awaited) {
    least_awaited = least_unacked;
  }
  return least_awaited;
}

QuicPacketNumber QuicSentPacketManager::GetLargestPacketPeerKnowsIsAcked(
    EncryptionLevel decrypted_packet_level) const {
  DCHECK(supports_multiple_packet_number_spaces());
  return largest_packets_peer_knows_is_acked_[QuicUtils::GetPacketNumberSpace(
      decrypted_packet_level)];
}

QuicTime::Delta
QuicSentPacketManager::GetNConsecutiveRetransmissionTimeoutDelay(
    int num_timeouts) const {
  QuicTime::Delta total_delay = QuicTime::Delta::Zero();
  const QuicTime::Delta srtt = rtt_stats_.SmoothedOrInitialRtt();
  int num_tlps =
      std::min(num_timeouts, static_cast<int>(max_tail_loss_probes_));
  num_timeouts -= num_tlps;
  if (num_tlps > 0) {
    if (enable_half_rtt_tail_loss_probe_ &&
        unacked_packets().HasUnackedStreamData()) {
      total_delay = total_delay + std::max(min_tlp_timeout_, srtt * 0.5);
      --num_tlps;
    }
    if (num_tlps > 0) {
      const QuicTime::Delta tlp_delay =
          std::max(2 * srtt, unacked_packets_.HasMultipleInFlightPackets()
                                 ? min_tlp_timeout_
                                 : (1.5 * srtt + (min_rto_timeout_ * 0.5)));
      total_delay = total_delay + num_tlps * tlp_delay;
    }
  }
  if (num_timeouts == 0) {
    return total_delay;
  }

  const QuicTime::Delta retransmission_delay =
      rtt_stats_.smoothed_rtt().IsZero()
          ? QuicTime::Delta::FromMilliseconds(kDefaultRetransmissionTimeMs)
          : std::max(srtt + 4 * rtt_stats_.mean_deviation(), min_rto_timeout_);
  total_delay = total_delay + ((1 << num_timeouts) - 1) * retransmission_delay;
  return total_delay;
}

bool QuicSentPacketManager::PeerCompletedAddressValidation() const {
  if (unacked_packets_.perspective() == Perspective::IS_SERVER ||
      !handshake_mode_disabled_) {
    return true;
  }

  // To avoid handshake deadlock due to anti-amplification limit, client needs
  // to set PTO timer until server successfully processed any HANDSHAKE packet.
  return handshake_finished_ || handshake_packet_acked_;
}

bool QuicSentPacketManager::IsLessThanThreePTOs(QuicTime::Delta timeout) const {
  return timeout < 3 * GetPtoDelay();
}

QuicTime::Delta QuicSentPacketManager::GetPtoDelay() const {
  return pto_enabled_ ? GetProbeTimeoutDelay(APPLICATION_DATA)
                      : GetRetransmissionDelay();
}

void QuicSentPacketManager::OnAckFrequencyFrameSent(
    const QuicAckFrequencyFrame& ack_frequency_frame) {
  in_use_sent_ack_delays_.emplace_back(ack_frequency_frame.max_ack_delay,
                                       ack_frequency_frame.sequence_number);
  if (ack_frequency_frame.max_ack_delay > peer_max_ack_delay_) {
    peer_max_ack_delay_ = ack_frequency_frame.max_ack_delay;
  }
}

void QuicSentPacketManager::OnAckFrequencyFrameAcked(
    const QuicAckFrequencyFrame& ack_frequency_frame) {
  int stale_entry_count = 0;
  for (auto it = in_use_sent_ack_delays_.cbegin();
       it != in_use_sent_ack_delays_.cend(); ++it) {
    if (it->second < ack_frequency_frame.sequence_number) {
      ++stale_entry_count;
    } else {
      break;
    }
  }
  if (stale_entry_count > 0) {
    in_use_sent_ack_delays_.pop_front_n(stale_entry_count);
  }
  if (in_use_sent_ack_delays_.empty()) {
    QUIC_BUG << "in_use_sent_ack_delays_ is empty.";
    return;
  }
  peer_max_ack_delay_ = std::max_element(in_use_sent_ack_delays_.cbegin(),
                                         in_use_sent_ack_delays_.cend())
                            ->first;
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
