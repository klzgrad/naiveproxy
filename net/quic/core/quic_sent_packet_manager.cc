// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_sent_packet_manager.h"

#include <algorithm>
#include <string>

#include "net/quic/chromium/quic_utils_chromium.h"
#include "net/quic/core/congestion_control/general_loss_algorithm.h"
#include "net/quic/core/congestion_control/pacing_sender.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/proto/cached_network_parameters.pb.h"
#include "net/quic/core/quic_connection_stats.h"
#include "net/quic/core/quic_pending_retransmission.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_map_util.h"

namespace net {

namespace {
static const int64_t kDefaultRetransmissionTimeMs = 500;
static const int64_t kMaxRetransmissionTimeMs = 60000;
// Maximum number of exponential backoffs used for RTO timeouts.
static const size_t kMaxRetransmissions = 10;
// Maximum number of packets retransmitted upon an RTO.
static const size_t kMaxRetransmissionsOnTimeout = 2;
// Minimum number of consecutive RTOs before path is considered to be degrading.
const size_t kMinTimeoutsBeforePathDegrading = 2;

// Ensure the handshake timer isnt't faster than 10ms.
// This limits the tenth retransmitted packet to 10s after the initial CHLO.
static const int64_t kMinHandshakeTimeoutMs = 10;

// Ensure the handshake timer isnt't faster than 25ms.
static const int64_t kConservativeMinHandshakeTimeoutMs = kMaxDelayedAckTimeMs;

// Sends up to two tail loss probes before firing an RTO,
// per draft RFC draft-dukkipati-tcpm-tcp-loss-probe.
static const size_t kDefaultMaxTailLossProbes = 2;

bool HasCryptoHandshake(const QuicTransmissionInfo& transmission_info) {
  DCHECK(!transmission_info.has_crypto_handshake ||
         !transmission_info.retransmittable_frames.empty());
  return transmission_info.has_crypto_handshake;
}

}  // namespace

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicSentPacketManager::QuicSentPacketManager(
    Perspective perspective,
    const QuicClock* clock,
    QuicConnectionStats* stats,
    CongestionControlType congestion_control_type,
    LossDetectionType loss_type)
    : unacked_packets_(),
      perspective_(perspective),
      clock_(clock),
      stats_(stats),
      debug_delegate_(nullptr),
      network_change_visitor_(nullptr),
      initial_congestion_window_(kInitialCongestionWindow),
      loss_algorithm_(&general_loss_algorithm_),
      general_loss_algorithm_(loss_type),
      n_connection_simulation_(false),
      least_packet_awaited_by_peer_(1),
      first_rto_transmission_(0),
      consecutive_rto_count_(0),
      consecutive_tlp_count_(0),
      consecutive_crypto_retransmission_count_(0),
      pending_timer_transmission_count_(0),
      max_tail_loss_probes_(kDefaultMaxTailLossProbes),
      enable_half_rtt_tail_loss_probe_(false),
      using_pacing_(false),
      use_new_rto_(false),
      conservative_handshake_retransmits_(false),
      largest_newly_acked_(0),
      largest_mtu_acked_(0),
      handshake_confirmed_(false),
      largest_packet_peer_knows_is_acked_(0) {
  SetSendAlgorithm(congestion_control_type);
}

QuicSentPacketManager::~QuicSentPacketManager() {}

void QuicSentPacketManager::SetFromConfig(const QuicConfig& config) {
  if (config.HasReceivedInitialRoundTripTimeUs() &&
      config.ReceivedInitialRoundTripTimeUs() > 0) {
    rtt_stats_.set_initial_rtt_us(
        std::max(kMinInitialRoundTripTimeUs,
                 std::min(kMaxInitialRoundTripTimeUs,
                          config.ReceivedInitialRoundTripTimeUs())));
  } else if (config.HasInitialRoundTripTimeUsToSend() &&
             config.GetInitialRoundTripTimeUsToSend() > 0) {
    rtt_stats_.set_initial_rtt_us(
        std::max(kMinInitialRoundTripTimeUs,
                 std::min(kMaxInitialRoundTripTimeUs,
                          config.GetInitialRoundTripTimeUsToSend())));
  }
  // Configure congestion control.
  if (config.HasClientRequestedIndependentOption(kTBBR, perspective_)) {
    SetSendAlgorithm(kBBR);
  }
  if (config.HasClientRequestedIndependentOption(kRENO, perspective_)) {
    if (config.HasClientRequestedIndependentOption(kBYTE, perspective_)) {
      SetSendAlgorithm(kRenoBytes);
    } else {
      SetSendAlgorithm(kReno);
    }
  } else if (config.HasClientRequestedIndependentOption(kBYTE, perspective_)) {
    SetSendAlgorithm(kCubic);
  } else if (FLAGS_quic_reloadable_flag_quic_default_to_bbr &&
             config.HasClientRequestedIndependentOption(kQBIC, perspective_)) {
    SetSendAlgorithm(kCubicBytes);
  } else if (FLAGS_quic_reloadable_flag_quic_enable_pcc &&
             config.HasClientRequestedIndependentOption(kTPCC, perspective_)) {
    SetSendAlgorithm(kPCC);
  }

  using_pacing_ = !FLAGS_quic_disable_pacing_for_perf_tests;

  if (config.HasClientSentConnectionOption(k1CON, perspective_)) {
    send_algorithm_->SetNumEmulatedConnections(1);
  }
  if (config.HasClientSentConnectionOption(kNCON, perspective_)) {
    n_connection_simulation_ = true;
  }
  if (config.HasClientSentConnectionOption(kNTLP, perspective_)) {
    max_tail_loss_probes_ = 0;
  }
  if (config.HasClientSentConnectionOption(kTLPR, perspective_)) {
    enable_half_rtt_tail_loss_probe_ = true;
  }
  if (config.HasClientSentConnectionOption(kNRTO, perspective_)) {
    use_new_rto_ = true;
  }
  // Configure loss detection.
  if (config.HasClientRequestedIndependentOption(kTIME, perspective_)) {
    general_loss_algorithm_.SetLossDetectionType(kTime);
  }
  if (config.HasClientRequestedIndependentOption(kATIM, perspective_)) {
    general_loss_algorithm_.SetLossDetectionType(kAdaptiveTime);
  }
  if (config.HasClientRequestedIndependentOption(kLFAK, perspective_)) {
    general_loss_algorithm_.SetLossDetectionType(kLazyFack);
  }
  if (config.HasClientSentConnectionOption(kCONH, perspective_)) {
    conservative_handshake_retransmits_ = true;
  }
  send_algorithm_->SetFromConfig(config, perspective_);

  if (network_change_visitor_ != nullptr) {
    network_change_visitor_->OnCongestionChange();
  }
}

void QuicSentPacketManager::ResumeConnectionState(
    const CachedNetworkParameters& cached_network_params,
    bool max_bandwidth_resumption) {
  if (cached_network_params.has_min_rtt_ms()) {
    uint32_t initial_rtt_us =
        kNumMicrosPerMilli * cached_network_params.min_rtt_ms();
    rtt_stats_.set_initial_rtt_us(
        std::max(kMinInitialRoundTripTimeUs,
                 std::min(kMaxInitialRoundTripTimeUs, initial_rtt_us)));
  }

  QuicBandwidth bandwidth = QuicBandwidth::FromBytesPerSecond(
      max_bandwidth_resumption
          ? cached_network_params.max_bandwidth_estimate_bytes_per_second()
          : cached_network_params.bandwidth_estimate_bytes_per_second());
  QuicTime::Delta rtt =
      QuicTime::Delta::FromMilliseconds(cached_network_params.min_rtt_ms());
  send_algorithm_->AdjustNetworkParameters(bandwidth, rtt);
}

void QuicSentPacketManager::SetNumOpenStreams(size_t num_streams) {
  if (n_connection_simulation_) {
    // Ensure the number of connections is between 1 and 5.
    send_algorithm_->SetNumEmulatedConnections(
        std::min<size_t>(5, std::max<size_t>(1, num_streams)));
  }
}

void QuicSentPacketManager::SetMaxPacingRate(QuicBandwidth max_pacing_rate) {
  pacing_sender_.set_max_pacing_rate(max_pacing_rate);
}

void QuicSentPacketManager::SetHandshakeConfirmed() {
  handshake_confirmed_ = true;
}

void QuicSentPacketManager::OnIncomingAck(const QuicAckFrame& ack_frame,
                                          QuicTime ack_receive_time) {
  DCHECK_LE(ack_frame.largest_observed, unacked_packets_.largest_sent_packet());
  QuicByteCount prior_in_flight = unacked_packets_.bytes_in_flight();
  UpdatePacketInformationReceivedByPeer(ack_frame);
  bool rtt_updated = MaybeUpdateRTT(ack_frame, ack_receive_time);
  DCHECK_GE(ack_frame.largest_observed, unacked_packets_.largest_observed());
  unacked_packets_.IncreaseLargestObserved(ack_frame.largest_observed);

  HandleAckForSentPackets(ack_frame);
  InvokeLossDetection(ack_receive_time);
  // Ignore losses in RTO mode.
  if (consecutive_rto_count_ > 0 && !use_new_rto_) {
    packets_lost_.clear();
  }
  MaybeInvokeCongestionEvent(rtt_updated, prior_in_flight, ack_receive_time);
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
      if (ack_frame.largest_observed < first_rto_transmission_) {
        // Replace SRTT with latest_rtt and increase the variance to prevent
        // a spurious RTO from happening again.
        rtt_stats_.ExpireSmoothedMetrics();
      } else {
        if (!use_new_rto_) {
          send_algorithm_->OnRetransmissionTimeout(true);
        }
      }
    }
    // Reset all retransmit counters any time a new packet is acked.
    consecutive_rto_count_ = 0;
    consecutive_tlp_count_ = 0;
    consecutive_crypto_retransmission_count_ = 0;
  }

  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnIncomingAck(ack_frame, ack_receive_time,
                                   unacked_packets_.largest_observed(),
                                   rtt_updated, GetLeastUnacked());
  }
}

void QuicSentPacketManager::UpdatePacketInformationReceivedByPeer(
    const QuicAckFrame& ack_frame) {
  if (ack_frame.packets.Empty()) {
    least_packet_awaited_by_peer_ = ack_frame.largest_observed + 1;
  } else {
    least_packet_awaited_by_peer_ = ack_frame.packets.Min();
  }
}

void QuicSentPacketManager::MaybeInvokeCongestionEvent(
    bool rtt_updated,
    QuicByteCount prior_in_flight,
    QuicTime event_time) {
  if (!rtt_updated && packets_acked_.empty() && packets_lost_.empty()) {
    return;
  }
  if (using_pacing_) {
    pacing_sender_.OnCongestionEvent(rtt_updated, prior_in_flight, event_time,
                                     packets_acked_, packets_lost_);
  } else {
    send_algorithm_->OnCongestionEvent(rtt_updated, prior_in_flight, event_time,
                                       packets_acked_, packets_lost_);
  }
  packets_acked_.clear();
  packets_lost_.clear();
  if (network_change_visitor_ != nullptr) {
    network_change_visitor_->OnCongestionChange();
  }
}

void QuicSentPacketManager::HandleAckForSentPackets(
    const QuicAckFrame& ack_frame) {
  // Go through the packets we have not received an ack for and see if this
  // incoming_ack shows they've been seen by the peer.
  QuicTime::Delta ack_delay_time = ack_frame.ack_delay_time;
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (packet_number > ack_frame.largest_observed) {
      // These packets are still in flight.
      break;
    }
    if (it->is_unackable) {
      continue;
    }
    if (!ack_frame.packets.Contains(packet_number)) {
      // Packet is still missing.
      continue;
    }
    // Packet was acked, so remove it from our unacked packet list.
    QUIC_DVLOG(1) << ENDPOINT << "Got an ack for packet " << packet_number;
    if (it->largest_acked > 0) {
      largest_packet_peer_knows_is_acked_ =
          std::max(largest_packet_peer_knows_is_acked_, it->largest_acked);
    }
    // If data is associated with the most recent transmission of this
    // packet, then inform the caller.
    if (it->in_flight) {
      packets_acked_.push_back(
          AckedPacket(packet_number, it->bytes_sent, QuicTime::Zero()));
    } else {
      // Unackable packets are skipped earlier.
      largest_newly_acked_ = packet_number;
    }
    MarkPacketHandled(packet_number, &(*it), ack_delay_time);
  }
}

void QuicSentPacketManager::RetransmitUnackedPackets(
    TransmissionType retransmission_type) {
  DCHECK(retransmission_type == ALL_UNACKED_RETRANSMISSION ||
         retransmission_type == ALL_INITIAL_RETRANSMISSION);
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (!it->retransmittable_frames.empty() &&
        (retransmission_type == ALL_UNACKED_RETRANSMISSION ||
         it->encryption_level == ENCRYPTION_INITIAL)) {
      MarkForRetransmission(packet_number, retransmission_type);
    }
  }
}

void QuicSentPacketManager::NeuterUnencryptedPackets() {
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (!it->retransmittable_frames.empty() &&
        it->encryption_level == ENCRYPTION_NONE) {
      // Once you're forward secure, no unencrypted packets will be sent, crypto
      // or otherwise. Unencrypted packets are neutered and abandoned, to ensure
      // they are not retransmitted or considered lost from a congestion control
      // perspective.
      pending_retransmissions_.erase(packet_number);
      unacked_packets_.RemoveFromInFlight(packet_number);
      unacked_packets_.RemoveRetransmittability(packet_number);
    }
  }
}

void QuicSentPacketManager::MarkForRetransmission(
    QuicPacketNumber packet_number,
    TransmissionType transmission_type) {
  const QuicTransmissionInfo& transmission_info =
      unacked_packets_.GetTransmissionInfo(packet_number);
  QUIC_BUG_IF(transmission_info.retransmittable_frames.empty());
  // Both TLP and the new RTO leave the packets in flight and let the loss
  // detection decide if packets are lost.
  if (transmission_type != TLP_RETRANSMISSION &&
      transmission_type != RTO_RETRANSMISSION) {
    unacked_packets_.RemoveFromInFlight(packet_number);
  }
    // TODO(ianswett): Currently the RTO can fire while there are pending NACK
    // retransmissions for the same data, which is not ideal.
  if (QuicContainsKey(pending_retransmissions_, packet_number)) {
    return;
    }

    pending_retransmissions_[packet_number] = transmission_type;
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

void QuicSentPacketManager::RecordSpuriousRetransmissions(
    const QuicTransmissionInfo& info,
    QuicPacketNumber acked_packet_number) {
  QuicPacketNumber retransmission = info.retransmission;
  while (retransmission != 0) {
    const QuicTransmissionInfo& retransmit_info =
        unacked_packets_.GetTransmissionInfo(retransmission);
    retransmission = retransmit_info.retransmission;
    RecordOneSpuriousRetransmission(retransmit_info);
  }
  // Only inform the loss detection of spurious retransmits it caused.
  if (unacked_packets_.GetTransmissionInfo(info.retransmission)
          .transmission_type == LOSS_RETRANSMISSION) {
    loss_algorithm_->SpuriousRetransmitDetected(
        unacked_packets_, clock_->Now(), rtt_stats_, info.retransmission);
  }
}

bool QuicSentPacketManager::HasPendingRetransmissions() const {
  return !pending_retransmissions_.empty();
}

QuicPendingRetransmission QuicSentPacketManager::NextPendingRetransmission() {
  QUIC_BUG_IF(pending_retransmissions_.empty())
      << "Unexpected call to NextPendingRetransmission() with empty pending "
      << "retransmission list. Corrupted memory usage imminent.";
  QuicPacketNumber packet_number = pending_retransmissions_.begin()->first;
  TransmissionType transmission_type = pending_retransmissions_.begin()->second;
  if (unacked_packets_.HasPendingCryptoPackets()) {
    // Ensure crypto packets are retransmitted before other packets.
    for (const auto& pair : pending_retransmissions_) {
      if (HasCryptoHandshake(
              unacked_packets_.GetTransmissionInfo(pair.first))) {
        packet_number = pair.first;
        transmission_type = pair.second;
        break;
      }
    }
  }
  DCHECK(unacked_packets_.IsUnacked(packet_number)) << packet_number;
  const QuicTransmissionInfo& transmission_info =
      unacked_packets_.GetTransmissionInfo(packet_number);
  DCHECK(!transmission_info.retransmittable_frames.empty());

  return QuicPendingRetransmission(packet_number, transmission_type,
                                   transmission_info.retransmittable_frames,
                                   transmission_info.has_crypto_handshake,
                                   transmission_info.num_padding_bytes,
                                   transmission_info.encryption_level,
                                   transmission_info.packet_number_length);
}

QuicPacketNumber QuicSentPacketManager::GetNewestRetransmission(
    QuicPacketNumber packet_number,
    const QuicTransmissionInfo& transmission_info) const {
  QuicPacketNumber retransmission = transmission_info.retransmission;
  while (retransmission != 0) {
    packet_number = retransmission;
    retransmission =
        unacked_packets_.GetTransmissionInfo(retransmission).retransmission;
  }
  return packet_number;
}

void QuicSentPacketManager::MarkPacketHandled(QuicPacketNumber packet_number,
                                              QuicTransmissionInfo* info,
                                              QuicTime::Delta ack_delay_time) {
  QuicPacketNumber newest_transmission =
      GetNewestRetransmission(packet_number, *info);
  // Remove the most recent packet, if it is pending retransmission.
  pending_retransmissions_.erase(newest_transmission);

  // The AckListener needs to be notified about the most recent
  // transmission, since that's the one only one it tracks.
  if (newest_transmission == packet_number) {
    unacked_packets_.NotifyStreamFramesAcked(*info, ack_delay_time);
    unacked_packets_.NotifyAndClearListeners(&info->ack_listeners,
                                             ack_delay_time);
  } else {
    unacked_packets_.NotifyAndClearListeners(newest_transmission,
                                             ack_delay_time);
    RecordSpuriousRetransmissions(*info, packet_number);
    // Remove the most recent packet from flight if it's a crypto handshake
    // packet, since they won't be acked now that one has been processed.
    // Other crypto handshake packets won't be in flight, only the newest
    // transmission of a crypto packet is in flight at once.
    // TODO(ianswett): Instead of handling all crypto packets special,
    // only handle nullptr encrypted packets in a special way.
    const QuicTransmissionInfo& newest_transmission_info =
        unacked_packets_.GetTransmissionInfo(newest_transmission);
    unacked_packets_.NotifyStreamFramesAcked(newest_transmission_info,
                                             ack_delay_time);
    if (HasCryptoHandshake(newest_transmission_info)) {
      unacked_packets_.RemoveFromInFlight(newest_transmission);
    }
  }

  if (network_change_visitor_ != nullptr &&
      info->bytes_sent > largest_mtu_acked_) {
    largest_mtu_acked_ = info->bytes_sent;
    network_change_visitor_->OnPathMtuIncreased(largest_mtu_acked_);
  }
  unacked_packets_.RemoveFromInFlight(info);
  unacked_packets_.RemoveRetransmittability(info);
  info->is_unackable = true;
}

bool QuicSentPacketManager::HasUnackedPackets() const {
  return unacked_packets_.HasUnackedPackets();
}

QuicPacketNumber QuicSentPacketManager::GetLeastUnacked() const {
  return unacked_packets_.GetLeastUnacked();
}

bool QuicSentPacketManager::OnPacketSent(
    SerializedPacket* serialized_packet,
    QuicPacketNumber original_packet_number,
    QuicTime sent_time,
    TransmissionType transmission_type,
    HasRetransmittableData has_retransmittable_data) {
  QuicPacketNumber packet_number = serialized_packet->packet_number;
  DCHECK_LT(0u, packet_number);
  DCHECK(!unacked_packets_.IsUnacked(packet_number));
  QUIC_BUG_IF(serialized_packet->encrypted_length == 0)
      << "Cannot send empty packets.";

  if (original_packet_number != 0) {
    pending_retransmissions_.erase(original_packet_number);
  }

  if (pending_timer_transmission_count_ > 0) {
    --pending_timer_transmission_count_;
  }

  bool in_flight = has_retransmittable_data == HAS_RETRANSMITTABLE_DATA;
  if (using_pacing_) {
    pacing_sender_.OnPacketSent(
        sent_time, unacked_packets_.bytes_in_flight(), packet_number,
        serialized_packet->encrypted_length, has_retransmittable_data);
  } else {
    send_algorithm_->OnPacketSent(
        sent_time, unacked_packets_.bytes_in_flight(), packet_number,
        serialized_packet->encrypted_length, has_retransmittable_data);
  }

  unacked_packets_.AddSentPacket(serialized_packet, original_packet_number,
                                 transmission_type, sent_time, in_flight);
  // Reset the retransmission timer anytime a pending packet is sent.
  return in_flight;
}

void QuicSentPacketManager::OnRetransmissionTimeout() {
  DCHECK(unacked_packets_.HasInFlightPackets());
  DCHECK_EQ(0u, pending_timer_transmission_count_);
  // Handshake retransmission, timer based loss detection, TLP, and RTO are
  // implemented with a single alarm. The handshake alarm is set when the
  // handshake has not completed, the loss alarm is set when the loss detection
  // algorithm says to, and the TLP and  RTO alarms are set after that.
  // The TLP alarm is always set to run for under an RTO.
  switch (GetRetransmissionMode()) {
    case HANDSHAKE_MODE:
      ++stats_->crypto_retransmit_count;
      RetransmitCryptoPackets();
      return;
    case LOSS_MODE: {
      ++stats_->loss_timeout_count;
      QuicByteCount prior_in_flight = unacked_packets_.bytes_in_flight();
      const QuicTime now = clock_->Now();
      InvokeLossDetection(now);
      MaybeInvokeCongestionEvent(false, prior_in_flight, now);
      return;
    }
    case TLP_MODE:
      // If no tail loss probe can be sent, because there are no retransmittable
      // packets, execute a conventional RTO to abandon old packets.
      ++stats_->tlp_count;
      ++consecutive_tlp_count_;
      pending_timer_transmission_count_ = 1;
      // TLPs prefer sending new data instead of retransmitting data, so
      // give the connection a chance to write before completing the TLP.
      return;
    case RTO_MODE:
      ++stats_->rto_count;
      RetransmitRtoPackets();
      if (network_change_visitor_ != nullptr &&
          consecutive_rto_count_ == kMinTimeoutsBeforePathDegrading) {
        network_change_visitor_->OnPathDegrading();
      }
      return;
  }
}

void QuicSentPacketManager::RetransmitCryptoPackets() {
  DCHECK_EQ(HANDSHAKE_MODE, GetRetransmissionMode());
  ++consecutive_crypto_retransmission_count_;
  bool packet_retransmitted = false;
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    // Only retransmit frames which are in flight, and therefore have been sent.
    if (!it->in_flight || it->retransmittable_frames.empty() ||
        !it->has_crypto_handshake) {
      continue;
    }
    packet_retransmitted = true;
    MarkForRetransmission(packet_number, HANDSHAKE_RETRANSMISSION);
    ++pending_timer_transmission_count_;
  }
  DCHECK(packet_retransmitted) << "No crypto packets found to retransmit.";
}

bool QuicSentPacketManager::MaybeRetransmitTailLossProbe() {
  if (pending_timer_transmission_count_ == 0) {
    return false;
  }
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    // Only retransmit frames which are in flight, and therefore have been sent.
    if (!it->in_flight || it->retransmittable_frames.empty()) {
      continue;
    }
    MarkForRetransmission(packet_number, TLP_RETRANSMISSION);
    return true;
  }
  QUIC_DLOG(ERROR)
      << "No retransmittable packets, so RetransmitOldestPacket failed.";
  return false;
}

void QuicSentPacketManager::RetransmitRtoPackets() {
  QUIC_BUG_IF(pending_timer_transmission_count_ > 0)
      << "Retransmissions already queued:" << pending_timer_transmission_count_;
  // Mark two packets for retransmission.
  QuicPacketNumber packet_number = unacked_packets_.GetLeastUnacked();
  for (QuicUnackedPacketMap::const_iterator it = unacked_packets_.begin();
       it != unacked_packets_.end(); ++it, ++packet_number) {
    if (!it->retransmittable_frames.empty() &&
        pending_timer_transmission_count_ < kMaxRetransmissionsOnTimeout) {
      MarkForRetransmission(packet_number, RTO_RETRANSMISSION);
      ++pending_timer_transmission_count_;
    }
    // Abandon non-retransmittable data that's in flight to ensure it doesn't
    // fill up the congestion window.
    const bool has_retransmissions = it->retransmission != 0;
    if (it->retransmittable_frames.empty() && it->in_flight &&
        !has_retransmissions) {
      // Log only for non-retransmittable data.
      // Retransmittable data is marked as lost during loss detection, and will
      // be logged later.
      unacked_packets_.RemoveFromInFlight(packet_number);
      if (debug_delegate_ != nullptr) {
        debug_delegate_->OnPacketLoss(packet_number, RTO_RETRANSMISSION,
                                      clock_->Now());
      }
    }
  }
  if (pending_timer_transmission_count_ > 0) {
    if (consecutive_rto_count_ == 0) {
      first_rto_transmission_ = unacked_packets_.largest_sent_packet() + 1;
    }
    ++consecutive_rto_count_;
  }
}

QuicSentPacketManager::RetransmissionTimeoutMode
QuicSentPacketManager::GetRetransmissionMode() const {
  DCHECK(unacked_packets_.HasInFlightPackets());
  if (!handshake_confirmed_ && unacked_packets_.HasPendingCryptoPackets()) {
    return HANDSHAKE_MODE;
  }
  if (loss_algorithm_->GetLossTimeout() != QuicTime::Zero()) {
    return LOSS_MODE;
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
  loss_algorithm_->DetectLosses(unacked_packets_, time, rtt_stats_,
                                largest_newly_acked_, &packets_lost_);
  for (const LostPacket& packet : packets_lost_) {
    ++stats_->packets_lost;
    if (debug_delegate_ != nullptr) {
      debug_delegate_->OnPacketLoss(packet.packet_number, LOSS_RETRANSMISSION,
                                    time);
    }

    // TODO(ianswett): This could be optimized.
    if (unacked_packets_.HasRetransmittableFrames(packet.packet_number)) {
      MarkForRetransmission(packet.packet_number, LOSS_RETRANSMISSION);
    } else {
      // Since we will not retransmit this, we need to remove it from
      // unacked_packets_.   This is either the current transmission of
      // a packet whose previous transmission has been acked or a packet that
      // has been TLP retransmitted.
      unacked_packets_.RemoveFromInFlight(packet.packet_number);
    }
  }
}

bool QuicSentPacketManager::MaybeUpdateRTT(const QuicAckFrame& ack_frame,
                                           QuicTime ack_receive_time) {
  // We rely on ack_delay_time to compute an RTT estimate, so we
  // only update rtt when the largest observed gets acked.
  // NOTE: If ack is a truncated ack, then the largest observed is in fact
  // unacked, and may cause an RTT sample to be taken.
  if (!unacked_packets_.IsUnacked(ack_frame.largest_observed)) {
    return false;
  }
  // We calculate the RTT based on the highest ACKed packet number, the lower
  // packet numbers will include the ACK aggregation delay.
  const QuicTransmissionInfo& transmission_info =
      unacked_packets_.GetTransmissionInfo(ack_frame.largest_observed);
  // Ensure the packet has a valid sent time.
  if (transmission_info.sent_time == QuicTime::Zero()) {
    QUIC_BUG << "Acked packet has zero sent time, largest_observed:"
             << ack_frame.largest_observed;
    return false;
  }

  QuicTime::Delta send_delta = ack_receive_time - transmission_info.sent_time;
  rtt_stats_.UpdateRtt(send_delta, ack_frame.ack_delay_time, ack_receive_time);

  return true;
}

QuicTime::Delta QuicSentPacketManager::TimeUntilSend(QuicTime now) {
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
  // Don't set the timer if there is nothing to retransmit or we've already
  // queued a tlp transmission and it hasn't been sent yet.
  if (!unacked_packets_.HasInFlightPackets() ||
      pending_timer_transmission_count_ > 0) {
    return QuicTime::Zero();
  }
  if (!unacked_packets_.HasUnackedRetransmittableFrames()) {
    return QuicTime::Zero();
  }
  switch (GetRetransmissionMode()) {
    case HANDSHAKE_MODE:
      return clock_->ApproximateNow() + GetCryptoRetransmissionDelay();
    case LOSS_MODE:
      return loss_algorithm_->GetLossTimeout();
    case TLP_MODE: {
      // TODO(ianswett): When CWND is available, it would be preferable to
      // set the timer based on the earliest retransmittable packet.
      // Base the updated timer on the send time of the last packet.
      const QuicTime sent_time = unacked_packets_.GetLastPacketSentTime();
      const QuicTime tlp_time = sent_time + GetTailLossProbeDelay();
      // Ensure the TLP timer never gets set to a time in the past.
      return std::max(clock_->ApproximateNow(), tlp_time);
    }
    case RTO_MODE: {
      // The RTO is based on the first outstanding packet.
      const QuicTime sent_time = unacked_packets_.GetLastPacketSentTime();
      QuicTime rto_time = sent_time + GetRetransmissionDelay();
      // Wait for TLP packets to be acked before an RTO fires.
      QuicTime tlp_time =
          unacked_packets_.GetLastPacketSentTime() + GetTailLossProbeDelay();
      return std::max(tlp_time, rto_time);
    }
  }
  DCHECK(false);
  return QuicTime::Zero();
}

const QuicTime::Delta QuicSentPacketManager::GetCryptoRetransmissionDelay()
    const {
  // This is equivalent to the TailLossProbeDelay, but slightly more aggressive
  // because crypto handshake messages don't incur a delayed ack time.
  QuicTime::Delta srtt = rtt_stats_.smoothed_rtt();
  int64_t delay_ms;
  if (srtt.IsZero()) {
    srtt = QuicTime::Delta::FromMicroseconds(rtt_stats_.initial_rtt_us());
  }
  if (conservative_handshake_retransmits_) {
    delay_ms = std::max(kConservativeMinHandshakeTimeoutMs,
                        static_cast<int64_t>(2 * srtt.ToMilliseconds()));
  } else {
    delay_ms = std::max(kMinHandshakeTimeoutMs,
                        static_cast<int64_t>(1.5 * srtt.ToMilliseconds()));
  }
  return QuicTime::Delta::FromMilliseconds(
      delay_ms << consecutive_crypto_retransmission_count_);
}

const QuicTime::Delta QuicSentPacketManager::GetTailLossProbeDelay() const {
  QuicTime::Delta srtt = rtt_stats_.smoothed_rtt();
  if (srtt.IsZero()) {
    srtt = QuicTime::Delta::FromMicroseconds(rtt_stats_.initial_rtt_us());
  }
  if (enable_half_rtt_tail_loss_probe_ && consecutive_tlp_count_ == 0u) {
    return QuicTime::Delta::FromMilliseconds(
        std::max(kMinTailLossProbeTimeoutMs,
                 static_cast<int64_t>(0.5 * srtt.ToMilliseconds())));
  }
  if (!unacked_packets_.HasMultipleInFlightPackets()) {
    return std::max(2 * srtt, 1.5 * srtt + QuicTime::Delta::FromMilliseconds(
                                               kMinRetransmissionTimeMs / 2));
  }
  return QuicTime::Delta::FromMilliseconds(
      std::max(kMinTailLossProbeTimeoutMs,
               static_cast<int64_t>(2 * srtt.ToMilliseconds())));
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
    if (retransmission_delay.ToMilliseconds() < kMinRetransmissionTimeMs) {
      retransmission_delay =
          QuicTime::Delta::FromMilliseconds(kMinRetransmissionTimeMs);
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

const RttStats* QuicSentPacketManager::GetRttStats() const {
  return &rtt_stats_;
}

QuicBandwidth QuicSentPacketManager::BandwidthEstimate() const {
  // TODO(ianswett): Remove BandwidthEstimate from SendAlgorithmInterface
  // and implement the logic here.
  return send_algorithm_->BandwidthEstimate();
}

const QuicSustainedBandwidthRecorder*
QuicSentPacketManager::SustainedBandwidthRecorder() const {
  return &sustained_bandwidth_recorder_;
}

QuicPacketCount QuicSentPacketManager::EstimateMaxPacketsInFlight(
    QuicByteCount max_packet_length) const {
  return send_algorithm_->GetCongestionWindow() / max_packet_length;
}

QuicPacketCount QuicSentPacketManager::GetCongestionWindowInTcpMss() const {
  return send_algorithm_->GetCongestionWindow() / kDefaultTCPMSS;
}

QuicByteCount QuicSentPacketManager::GetCongestionWindowInBytes() const {
  return send_algorithm_->GetCongestionWindow();
}

QuicPacketCount QuicSentPacketManager::GetSlowStartThresholdInTcpMss() const {
  return send_algorithm_->GetSlowStartThreshold() / kDefaultTCPMSS;
}

std::string QuicSentPacketManager::GetDebugState() const {
  return send_algorithm_->GetDebugState();
}

QuicByteCount QuicSentPacketManager::GetBytesInFlight() const {
  return unacked_packets_.bytes_in_flight();
}

void QuicSentPacketManager::CancelRetransmissionsForStream(
    QuicStreamId stream_id) {
  unacked_packets_.CancelRetransmissionsForStream(stream_id);
  PendingRetransmissionMap::iterator it = pending_retransmissions_.begin();
  while (it != pending_retransmissions_.end()) {
    if (unacked_packets_.HasRetransmittableFrames(it->first)) {
      ++it;
      continue;
    }
    it = pending_retransmissions_.erase(it);
  }
}

void QuicSentPacketManager::SetSendAlgorithm(
    CongestionControlType congestion_control_type) {
  SetSendAlgorithm(SendAlgorithmInterface::Create(
      clock_, &rtt_stats_, &unacked_packets_, congestion_control_type,
      QuicRandom::GetInstance(), stats_, initial_congestion_window_));
}

void QuicSentPacketManager::SetSendAlgorithm(
    SendAlgorithmInterface* send_algorithm) {
  send_algorithm_.reset(send_algorithm);
  pacing_sender_.set_sender(send_algorithm);
}

void QuicSentPacketManager::OnConnectionMigration(PeerAddressChangeType type) {
  if (type == PORT_CHANGE || type == IPV4_SUBNET_CHANGE) {
    // Rtt and cwnd do not need to be reset when the peer address change is
    // considered to be caused by NATs.
    return;
  }
  consecutive_rto_count_ = 0;
  consecutive_tlp_count_ = 0;
  rtt_stats_.OnConnectionMigration();
  send_algorithm_->OnConnectionMigration();
}

void QuicSentPacketManager::SetDebugDelegate(DebugDelegate* debug_delegate) {
  debug_delegate_ = debug_delegate;
}

QuicPacketNumber QuicSentPacketManager::GetLargestObserved() const {
  return unacked_packets_.largest_observed();
}

QuicPacketNumber QuicSentPacketManager::GetLargestSentPacket() const {
  return unacked_packets_.largest_sent_packet();
}

void QuicSentPacketManager::SetNetworkChangeVisitor(
    NetworkChangeVisitor* visitor) {
  DCHECK(!network_change_visitor_);
  DCHECK(visitor);
  network_change_visitor_ = visitor;
}

bool QuicSentPacketManager::InSlowStart() const {
  return send_algorithm_->InSlowStart();
}

size_t QuicSentPacketManager::GetConsecutiveRtoCount() const {
  return consecutive_rto_count_;
}

size_t QuicSentPacketManager::GetConsecutiveTlpCount() const {
  return consecutive_tlp_count_;
}

void QuicSentPacketManager::OnApplicationLimited() {
  send_algorithm_->OnApplicationLimited(unacked_packets_.bytes_in_flight());
}

const SendAlgorithmInterface* QuicSentPacketManager::GetSendAlgorithm() const {
  return send_algorithm_.get();
}

void QuicSentPacketManager::SetStreamNotifier(
    StreamNotifierInterface* stream_notifier) {
  unacked_packets_.SetStreamNotifier(stream_notifier);
}

}  // namespace net
