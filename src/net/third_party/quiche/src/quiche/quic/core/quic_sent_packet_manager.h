// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_SENT_PACKET_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_SENT_PACKET_MANAGER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "quiche/quic/core/congestion_control/pacing_sender.h"
#include "quiche/quic/core/congestion_control/rtt_stats.h"
#include "quiche/quic/core/congestion_control/send_algorithm_interface.h"
#include "quiche/quic/core/congestion_control/uber_loss_algorithm.h"
#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_sustained_bandwidth_recorder.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_transmission_info.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_unacked_packet_map.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/quiche_circular_deque.h"

namespace quic {

namespace test {
class QuicConnectionPeer;
class QuicSentPacketManagerPeer;
}  // namespace test

class QuicClock;
class QuicConfig;
struct QuicConnectionStats;

// Class which tracks the set of packets sent on a QUIC connection and contains
// a send algorithm to decide when to send new packets.  It keeps track of any
// retransmittable data associated with each packet. If a packet is
// retransmitted, it will keep track of each version of a packet so that if a
// previous transmission is acked, the data will not be retransmitted.
class QUICHE_EXPORT QuicSentPacketManager {
 public:
  // Interface which gets callbacks from the QuicSentPacketManager at
  // interesting points.  Implementations must not mutate the state of
  // the packet manager or connection as a result of these callbacks.
  class QUICHE_EXPORT DebugDelegate {
   public:
    struct QUICHE_EXPORT SendParameters {
      CongestionControlType congestion_control_type;
      bool use_pacing;
      QuicPacketCount initial_congestion_window;
    };

    virtual ~DebugDelegate() {}

    // Called when a spurious retransmission is detected.
    virtual void OnSpuriousPacketRetransmission(
        TransmissionType /*transmission_type*/, QuicByteCount /*byte_size*/) {}

    virtual void OnIncomingAck(QuicPacketNumber /*ack_packet_number*/,
                               EncryptionLevel /*ack_decrypted_level*/,
                               const QuicAckFrame& /*ack_frame*/,
                               QuicTime /*ack_receive_time*/,
                               QuicPacketNumber /*largest_observed*/,
                               bool /*rtt_updated*/,
                               QuicPacketNumber /*least_unacked_sent_packet*/) {
    }

    virtual void OnPacketLoss(QuicPacketNumber /*lost_packet_number*/,
                              EncryptionLevel /*encryption_level*/,
                              TransmissionType /*transmission_type*/,
                              QuicTime /*detection_time*/) {}

    virtual void OnApplicationLimited() {}

    virtual void OnAdjustNetworkParameters(QuicBandwidth /*bandwidth*/,
                                           QuicTime::Delta /*rtt*/,
                                           QuicByteCount /*old_cwnd*/,
                                           QuicByteCount /*new_cwnd*/) {}

    virtual void OnOvershootingDetected() {}

    virtual void OnConfigProcessed(const SendParameters& /*parameters*/) {}

    virtual void OnSendAlgorithmChanged(CongestionControlType /*type*/) {}
  };

  // Interface which gets callbacks from the QuicSentPacketManager when
  // network-related state changes. Implementations must not mutate the
  // state of the packet manager as a result of these callbacks.
  class QUICHE_EXPORT NetworkChangeVisitor {
   public:
    virtual ~NetworkChangeVisitor() {}

    // Called when congestion window or RTT may have changed.
    virtual void OnCongestionChange() = 0;

    // Called when the Path MTU may have increased.
    virtual void OnPathMtuIncreased(QuicPacketLength packet_size) = 0;

    // Called when a in-flight packet sent on the current default path with ECN
    // markings is acked.
    virtual void OnInFlightEcnPacketAcked() = 0;

    // Called when an ACK frame with ECN counts has invalid values, or an ACK
    // acknowledges packets with ECN marks and there are no ECN counts.
    virtual void OnInvalidEcnFeedback() = 0;
  };

  // The retransmission timer is a single timer which switches modes depending
  // upon connection state.
  enum RetransmissionTimeoutMode {
    // Retransmission of handshake packets prior to handshake completion.
    HANDSHAKE_MODE,
    // Re-invoke the loss detection when a packet is not acked before the
    // loss detection algorithm expects.
    LOSS_MODE,
    // A probe timeout. At least one probe packet must be sent when timer
    // expires.
    PTO_MODE,
  };

  QuicSentPacketManager(Perspective perspective, const QuicClock* clock,
                        QuicRandom* random, QuicConnectionStats* stats,
                        CongestionControlType congestion_control_type);
  QuicSentPacketManager(const QuicSentPacketManager&) = delete;
  QuicSentPacketManager& operator=(const QuicSentPacketManager&) = delete;
  virtual ~QuicSentPacketManager();

  virtual void SetFromConfig(const QuicConfig& config);

  void ReserveUnackedPacketsInitialCapacity(int initial_capacity) {
    unacked_packets_.ReserveInitialCapacity(initial_capacity);
  }

  void ApplyConnectionOptions(const QuicTagVector& connection_options);

  // Pass the CachedNetworkParameters to the send algorithm.
  void ResumeConnectionState(
      const CachedNetworkParameters& cached_network_params,
      bool max_bandwidth_resumption);

  void SetMaxPacingRate(QuicBandwidth max_pacing_rate) {
    pacing_sender_.set_max_pacing_rate(max_pacing_rate);
  }

  // The delay to use for the send alarm. If zero, it essentially means
  // to queue the send call immediately.
  // WARNING: This is currently an experimental API.
  // TODO(genioshelo): This should implement a dynamic delay based on the
  // underlying connection properties and lumpy pacing.
  QuicTime::Delta GetDeferredSendAlarmDelay() const {
    return deferred_send_alarm_delay_.value_or(QuicTime::Delta::Zero());
  }
  void SetDeferredSendAlarmDelay(QuicTime::Delta delay) {
    deferred_send_alarm_delay_ = delay;
  }

  QuicBandwidth MaxPacingRate() const {
    return pacing_sender_.max_pacing_rate();
  }

  // Called to mark the handshake state complete, and all handshake packets are
  // neutered.
  // TODO(fayang): Rename this function to OnHandshakeComplete.
  void SetHandshakeConfirmed();

  // Requests retransmission of all unacked 0-RTT packets.
  // Only 0-RTT encrypted packets will be retransmitted. This can happen,
  // for example, when a CHLO has been rejected and the previously encrypted
  // data needs to be encrypted with a new key.
  void MarkZeroRttPacketsForRetransmission();

  // Request retransmission of all unacked INITIAL packets.
  void MarkInitialPacketsForRetransmission();

  // Notify the sent packet manager of an external network measurement or
  // prediction for either |bandwidth| or |rtt|; either can be empty.
  void AdjustNetworkParameters(
      const SendAlgorithmInterface::NetworkParams& params);

  void SetLossDetectionTuner(
      std::unique_ptr<LossDetectionTunerInterface> tuner);
  void OnConfigNegotiated();
  void OnConnectionClosed();

  // Retransmits the oldest pending packet.
  bool MaybeRetransmitOldestPacket(TransmissionType type);

  // Removes the retransmittable frames from all unencrypted packets to ensure
  // they don't get retransmitted.
  void NeuterUnencryptedPackets();

  // Returns true if there's outstanding crypto data.
  bool HasUnackedCryptoPackets() const {
    return unacked_packets_.HasPendingCryptoPackets();
  }

  // Returns true if there are packets in flight expecting to be acknowledged.
  bool HasInFlightPackets() const {
    return unacked_packets_.HasInFlightPackets();
  }

  // Returns the smallest packet number of a serialized packet which has not
  // been acked by the peer.
  QuicPacketNumber GetLeastUnacked() const {
    return unacked_packets_.GetLeastUnacked();
  }

  // Called when we have sent bytes to the peer.  This informs the manager both
  // the number of bytes sent and if they were retransmitted and if this packet
  // is used for rtt measuring.  Returns true if the sender should reset the
  // retransmission timer.
  bool OnPacketSent(SerializedPacket* mutable_packet, QuicTime sent_time,
                    TransmissionType transmission_type,
                    HasRetransmittableData has_retransmittable_data,
                    bool measure_rtt, QuicEcnCodepoint ecn_codepoint);

  bool CanSendAckFrequency() const;

  QuicAckFrequencyFrame GetUpdatedAckFrequencyFrame() const;

  // Called when the retransmission timer expires and returns the retransmission
  // mode.
  RetransmissionTimeoutMode OnRetransmissionTimeout();

  // Calculate the time until we can send the next packet to the wire.
  // Note 1: When kUnknownWaitTime is returned, there is no need to poll
  // TimeUntilSend again until we receive an OnIncomingAckFrame event.
  // Note 2: Send algorithms may or may not use |retransmit| in their
  // calculations.
  QuicTime::Delta TimeUntilSend(QuicTime now) const;

  // Returns the current delay for the retransmission timer, which may send
  // either a tail loss probe or do a full RTO.  Returns QuicTime::Zero() if
  // there are no retransmittable packets.
  const QuicTime GetRetransmissionTime() const;

  // Returns the current delay for the path degrading timer, which is used to
  // notify the session that this connection is degrading.
  const QuicTime::Delta GetPathDegradingDelay() const;

  // Returns the current delay for detecting network blackhole.
  const QuicTime::Delta GetNetworkBlackholeDelay(
      int8_t num_rtos_for_blackhole_detection) const;

  // Returns the delay before reducing max packet size. This delay is guranteed
  // to be smaller than the network blackhole delay.
  QuicTime::Delta GetMtuReductionDelay(
      int8_t num_rtos_for_blackhole_detection) const;

  const RttStats* GetRttStats() const { return &rtt_stats_; }

  void SetRttStats(const RttStats& rtt_stats) {
    rtt_stats_.CloneFrom(rtt_stats);
  }

  // Returns the estimated bandwidth calculated by the congestion algorithm.
  QuicBandwidth BandwidthEstimate() const {
    return send_algorithm_->BandwidthEstimate();
  }

  const QuicSustainedBandwidthRecorder* SustainedBandwidthRecorder() const {
    return &sustained_bandwidth_recorder_;
  }

  // Returns the size of the current congestion window in number of
  // kDefaultTCPMSS-sized segments. Note, this is not the *available* window.
  // Some send algorithms may not use a congestion window and will return 0.
  QuicPacketCount GetCongestionWindowInTcpMss() const {
    return send_algorithm_->GetCongestionWindow() / kDefaultTCPMSS;
  }

  // Returns the number of packets of length |max_packet_length| which fit in
  // the current congestion window. More packets may end up in flight if the
  // congestion window has been recently reduced, of if non-full packets are
  // sent.
  QuicPacketCount EstimateMaxPacketsInFlight(
      QuicByteCount max_packet_length) const {
    return send_algorithm_->GetCongestionWindow() / max_packet_length;
  }

  // Returns the size of the current congestion window size in bytes.
  QuicByteCount GetCongestionWindowInBytes() const {
    return send_algorithm_->GetCongestionWindow();
  }

  // Returns the difference between current congestion window and bytes in
  // flight. Returns 0 if bytes in flight is bigger than the current congestion
  // window.
  QuicByteCount GetAvailableCongestionWindowInBytes() const;

  QuicBandwidth GetPacingRate() const {
    return send_algorithm_->PacingRate(GetBytesInFlight());
  }

  // Returns the size of the slow start congestion window in nume of 1460 byte
  // TCP segments, aka ssthresh.  Some send algorithms do not define a slow
  // start threshold and will return 0.
  QuicPacketCount GetSlowStartThresholdInTcpMss() const {
    return send_algorithm_->GetSlowStartThreshold() / kDefaultTCPMSS;
  }

  // Return the total time spent in slow start so far. If the sender is
  // currently in slow start, the return value will include the duration between
  // the most recent entry to slow start and now.
  //
  // Only implemented for BBR. Return QuicTime::Delta::Infinite() for other
  // congestion controllers.
  QuicTime::Delta GetSlowStartDuration() const;

  // Returns debugging information about the state of the congestion controller.
  std::string GetDebugState() const;

  // Returns the number of bytes that are considered in-flight, i.e. not lost or
  // acknowledged.
  QuicByteCount GetBytesInFlight() const {
    return unacked_packets_.bytes_in_flight();
  }

  // Called when peer address changes. Must be called IFF the address change is
  // not NAT rebinding. If reset_send_algorithm is true, switch to a new send
  // algorithm object and retransmit all the in-flight packets. Return the send
  // algorithm object used on the previous path.
  std::unique_ptr<SendAlgorithmInterface> OnConnectionMigration(
      bool reset_send_algorithm);

  // Called when an ack frame is initially parsed.
  void OnAckFrameStart(QuicPacketNumber largest_acked,
                       QuicTime::Delta ack_delay_time,
                       QuicTime ack_receive_time);

  // Called when ack range [start, end) is received. Populates packets_acked_
  // with newly acked packets.
  void OnAckRange(QuicPacketNumber start, QuicPacketNumber end);

  // Called when a timestamp is processed.  If it's present in packets_acked_,
  // the timestamp field is set.  Otherwise, the timestamp is ignored.
  void OnAckTimestamp(QuicPacketNumber packet_number, QuicTime timestamp);

  // Called when an ack frame is parsed completely.
  AckResult OnAckFrameEnd(QuicTime ack_receive_time,
                          QuicPacketNumber ack_packet_number,
                          EncryptionLevel ack_decrypted_level,
                          const std::optional<QuicEcnCounts>& ecn_counts);

  void EnableMultiplePacketNumberSpacesSupport();

  void SetDebugDelegate(DebugDelegate* debug_delegate);

  QuicPacketNumber GetLargestObserved() const {
    return unacked_packets_.largest_acked();
  }

  QuicPacketNumber GetLargestAckedPacket(
      EncryptionLevel decrypted_packet_level) const;

  QuicPacketNumber GetLargestSentPacket() const {
    return unacked_packets_.largest_sent_packet();
  }

  // Returns the lowest of the largest acknowledged packet and the least
  // unacked packet. This is designed to be used when computing the packet
  // number length to send.
  QuicPacketNumber GetLeastPacketAwaitedByPeer(
      EncryptionLevel encryption_level) const;

  QuicPacketNumber GetLargestPacketPeerKnowsIsAcked(
      EncryptionLevel decrypted_packet_level) const;

  void SetNetworkChangeVisitor(NetworkChangeVisitor* visitor) {
    QUICHE_DCHECK(!network_change_visitor_);
    QUICHE_DCHECK(visitor);
    network_change_visitor_ = visitor;
  }

  bool InSlowStart() const { return send_algorithm_->InSlowStart(); }

  size_t GetConsecutivePtoCount() const { return consecutive_pto_count_; }

  void OnApplicationLimited();

  const SendAlgorithmInterface* GetSendAlgorithm() const {
    return send_algorithm_.get();
  }

  // Wrapper for SendAlgorithmInterface functions, since these functions are
  // not const.
  bool EnableECT0() { return send_algorithm_->EnableECT0(); }
  bool EnableECT1() { return send_algorithm_->EnableECT1(); }

  void SetSessionNotifier(SessionNotifierInterface* session_notifier) {
    unacked_packets_.SetSessionNotifier(session_notifier);
  }

  NextReleaseTimeResult GetNextReleaseTime() const;

  QuicPacketCount initial_congestion_window() const {
    return initial_congestion_window_;
  }

  QuicPacketNumber largest_packet_peer_knows_is_acked() const {
    QUICHE_DCHECK(!supports_multiple_packet_number_spaces());
    return largest_packet_peer_knows_is_acked_;
  }

  size_t pending_timer_transmission_count() const {
    return pending_timer_transmission_count_;
  }

  QuicTime::Delta peer_max_ack_delay() const { return peer_max_ack_delay_; }

  void set_peer_max_ack_delay(QuicTime::Delta peer_max_ack_delay) {
    // The delayed ack time should never be more than one half the min RTO time.
    QUICHE_DCHECK_LE(
        peer_max_ack_delay,
        (QuicTime::Delta::FromMilliseconds(kMinRetransmissionTimeMs) * 0.5));
    peer_max_ack_delay_ = peer_max_ack_delay;
  }

  const QuicUnackedPacketMap& unacked_packets() const {
    return unacked_packets_;
  }

  const UberLossAlgorithm* uber_loss_algorithm() const {
    return &uber_loss_algorithm_;
  }

  // Sets the send algorithm to the given congestion control type and points the
  // pacing sender at |send_algorithm_|. Can be called any number of times.
  void SetSendAlgorithm(CongestionControlType congestion_control_type);

  // Sets the send algorithm to |send_algorithm| and points the pacing sender at
  // |send_algorithm_|. Takes ownership of |send_algorithm|. Can be called any
  // number of times.
  // Setting the send algorithm once the connection is underway is dangerous.
  void SetSendAlgorithm(SendAlgorithmInterface* send_algorithm);

  // Sends one probe packet.
  void MaybeSendProbePacket();

  // Called to disable HANDSHAKE_MODE, and only PTO and LOSS modes are used.
  // Also enable IETF loss detection.
  void EnableIetfPtoAndLossDetection();

  // Called to retransmit in flight packet of |space| if any.
  void RetransmitDataOfSpaceIfAny(PacketNumberSpace space);

  // Returns true if |timeout| is less than 3 * RTO/PTO delay.
  bool IsLessThanThreePTOs(QuicTime::Delta timeout) const;

  // Returns current PTO delay.
  QuicTime::Delta GetPtoDelay() const;

  bool supports_multiple_packet_number_spaces() const {
    return unacked_packets_.supports_multiple_packet_number_spaces();
  }

  bool handshake_mode_disabled() const { return handshake_mode_disabled_; }

  bool zero_rtt_packet_acked() const { return zero_rtt_packet_acked_; }

  bool one_rtt_packet_acked() const { return one_rtt_packet_acked_; }

  void OnUserAgentIdKnown() { loss_algorithm_->OnUserAgentIdKnown(); }

  // Gets the earliest in flight packet sent time to calculate PTO. Also
  // updates |packet_number_space| if a PTO timer should be armed.
  QuicTime GetEarliestPacketSentTimeForPto(
      PacketNumberSpace* packet_number_space) const;

  void set_num_ptos_for_path_degrading(int num_ptos_for_path_degrading) {
    num_ptos_for_path_degrading_ = num_ptos_for_path_degrading;
  }

  // Sets the initial RTT of the connection. The inital RTT is clamped to
  // - A maximum of kMaxInitialRoundTripTimeUs.
  // - A minimum of kMinTrustedInitialRoundTripTimeUs if |trusted|, or
  // kMinUntrustedInitialRoundTripTimeUs if not |trusted|.
  void SetInitialRtt(QuicTime::Delta rtt, bool trusted);

 private:
  friend class test::QuicConnectionPeer;
  friend class test::QuicSentPacketManagerPeer;

  // Returns the current retransmission mode.
  RetransmissionTimeoutMode GetRetransmissionMode() const;

  // Retransmits all crypto stream packets.
  void RetransmitCryptoPackets();

  // Returns the timeout for retransmitting crypto handshake packets.
  const QuicTime::Delta GetCryptoRetransmissionDelay() const;

  // Returns the probe timeout.
  const QuicTime::Delta GetProbeTimeoutDelay(PacketNumberSpace space) const;

  // Update the RTT if the ack is for the largest acked packet number.
  // Returns true if the rtt was updated.
  bool MaybeUpdateRTT(QuicPacketNumber largest_acked,
                      QuicTime::Delta ack_delay_time,
                      QuicTime ack_receive_time);

  // Invokes the loss detection algorithm and loses and retransmits packets if
  // necessary.
  void InvokeLossDetection(QuicTime time);

  // Invokes OnCongestionEvent if |rtt_updated| is true, there are pending acks,
  // or pending losses.  Clears pending acks and pending losses afterwards.
  // |prior_in_flight| is the number of bytes in flight before the losses or
  // acks, |event_time| is normally the timestamp of the ack packet which caused
  // the event, although it can be the time at which loss detection was
  // triggered.
  void MaybeInvokeCongestionEvent(bool rtt_updated,
                                  QuicByteCount prior_in_flight,
                                  QuicTime event_time,
                                  std::optional<QuicEcnCounts> ecn_counts,
                                  const QuicEcnCounts& previous_counts);

  // Removes the retransmittability and in flight properties from the packet at
  // |info| due to receipt by the peer.
  void MarkPacketHandled(QuicPacketNumber packet_number,
                         QuicTransmissionInfo* info, QuicTime ack_receive_time,
                         QuicTime::Delta ack_delay_time,
                         QuicTime receive_timestamp);

  // Request that |packet_number| be retransmitted after the other pending
  // retransmissions.  Does not add it to the retransmissions if it's already
  // a pending retransmission. Do not reuse iterator of the underlying
  // unacked_packets_ after calling this function as it can be invalidated.
  void MarkForRetransmission(QuicPacketNumber packet_number,
                             TransmissionType transmission_type);

  // Called after packets have been marked handled with last received ack frame.
  void PostProcessNewlyAckedPackets(QuicPacketNumber ack_packet_number,
                                    EncryptionLevel ack_decrypted_level,
                                    const QuicAckFrame& ack_frame,
                                    QuicTime ack_receive_time, bool rtt_updated,
                                    QuicByteCount prior_bytes_in_flight,
                                    std::optional<QuicEcnCounts> ecn_counts);

  // Notify observers that packet with QuicTransmissionInfo |info| is a spurious
  // retransmission. It is caller's responsibility to guarantee the packet with
  // QuicTransmissionInfo |info| is a spurious retransmission before calling
  // this function.
  void RecordOneSpuriousRetransmission(const QuicTransmissionInfo& info);

  // Called when handshake is confirmed to remove the retransmittable frames
  // from all packets of HANDSHAKE_DATA packet number space to ensure they don't
  // get retransmitted and will eventually be removed from unacked packets map.
  void NeuterHandshakePackets();

  // Indicates whether including peer_max_ack_delay_ when calculating PTO
  // timeout.
  bool ShouldAddMaxAckDelay(PacketNumberSpace space) const;

  // A helper function to return total delay of |num_timeouts| retransmission
  // timeout with TLP and RTO mode.
  // TODO(fayang): remove this method and calculate blackhole delay by PTO.
  QuicTime::Delta GetNConsecutiveRetransmissionTimeoutDelay(
      int num_timeouts) const;

  // Returns true if peer has finished address validation, such that
  // retransmission timer is not armed if there is no packets in flight.
  bool PeerCompletedAddressValidation() const;

  // Called when an AckFrequencyFrame is sent.
  void OnAckFrequencyFrameSent(
      const QuicAckFrequencyFrame& ack_frequency_frame);

  // Called when an AckFrequencyFrame is acked.
  void OnAckFrequencyFrameAcked(
      const QuicAckFrequencyFrame& ack_frequency_frame);

  // Checks if newly reported ECN counts are valid given what has been reported
  // in the past. |space| is the packet number space the counts apply to.
  // |ecn_counts| is what the peer reported. |newly_acked_ect0| and
  // |newly_acked_ect1| count the number of previously unacked packets with
  // those markings that appeared in an ack block for the first time.
  bool IsEcnFeedbackValid(PacketNumberSpace space,
                          const std::optional<QuicEcnCounts>& ecn_counts,
                          QuicPacketCount newly_acked_ect0,
                          QuicPacketCount newly_acked_ect1);

  // Update counters for the number of ECN-marked packets sent.
  void RecordEcnMarkingSent(QuicEcnCodepoint ecn_codepoint,
                            EncryptionLevel level);

  // Newly serialized retransmittable packets are added to this map, which
  // contains owning pointers to any contained frames.  If a packet is
  // retransmitted, this map will contain entries for both the old and the new
  // packet. The old packet's retransmittable frames entry will be nullptr,
  // while the new packet's entry will contain the frames to retransmit.
  // If the old packet is acked before the new packet, then the old entry will
  // be removed from the map and the new entry's retransmittable frames will be
  // set to nullptr.
  QuicUnackedPacketMap unacked_packets_;

  const QuicClock* clock_;
  QuicRandom* random_;
  QuicConnectionStats* stats_;

  DebugDelegate* debug_delegate_;
  NetworkChangeVisitor* network_change_visitor_;
  QuicPacketCount initial_congestion_window_;
  RttStats rtt_stats_;
  std::unique_ptr<SendAlgorithmInterface> send_algorithm_;
  // Not owned. Always points to |uber_loss_algorithm_| outside of tests.
  LossDetectionInterface* loss_algorithm_;
  UberLossAlgorithm uber_loss_algorithm_;

  // Number of times the crypto handshake has been retransmitted.
  size_t consecutive_crypto_retransmission_count_;
  // Number of pending transmissions of PTO or crypto packets.
  size_t pending_timer_transmission_count_;

  bool using_pacing_;
  // If true, use a more conservative handshake retransmission policy.
  bool conservative_handshake_retransmits_;

  // Vectors packets acked and lost as a result of the last congestion event.
  AckedPacketVector packets_acked_;
  LostPacketVector packets_lost_;
  // Largest newly acknowledged packet.
  QuicPacketNumber largest_newly_acked_;
  // Largest packet in bytes ever acknowledged.
  QuicPacketLength largest_mtu_acked_;

  // Replaces certain calls to |send_algorithm_| when |using_pacing_| is true.
  // Calls into |send_algorithm_| for the underlying congestion control.
  PacingSender pacing_sender_;

  // Indicates whether handshake is finished. This is purely used to determine
  // retransmission mode. DONOT use this to infer handshake state.
  bool handshake_finished_;

  // Records bandwidth from server to client in normal operation, over periods
  // of time with no loss events.
  QuicSustainedBandwidthRecorder sustained_bandwidth_recorder_;

  // The largest acked value that was sent in an ack, which has then been acked.
  QuicPacketNumber largest_packet_peer_knows_is_acked_;
  // The largest acked value that was sent in an ack, which has then been acked
  // for per packet number space. Only used when connection supports multiple
  // packet number spaces.
  QuicPacketNumber
      largest_packets_peer_knows_is_acked_[NUM_PACKET_NUMBER_SPACES];

  // The maximum ACK delay time that the peer might uses. Initialized to be the
  // same as local_max_ack_delay_, may be changed via transport parameter
  // negotiation or subsequently by AckFrequencyFrame.
  QuicTime::Delta peer_max_ack_delay_;

  // Peer sends min_ack_delay in TransportParameter to advertise its support for
  // AckFrequencyFrame.
  QuicTime::Delta peer_min_ack_delay_ = QuicTime::Delta::Infinite();

  // Use smoothed RTT for computing max_ack_delay in AckFrequency frame.
  bool use_smoothed_rtt_in_ack_delay_ = false;

  // The history of outstanding max_ack_delays sent to peer. Outstanding means
  // a max_ack_delay is sent as part of the last acked AckFrequencyFrame or
  // an unacked AckFrequencyFrame after that.
  quiche::QuicheCircularDeque<
      std::pair<QuicTime::Delta, /*sequence_number=*/uint64_t>>
      in_use_sent_ack_delays_;

  // Latest received ack frame.
  QuicAckFrame last_ack_frame_;

  // Record whether RTT gets updated by last largest acked..
  bool rtt_updated_;

  // A reverse iterator of last_ack_frame_.packets. This is reset in
  // OnAckRangeStart, and gradually moves in OnAckRange..
  PacketNumberQueue::const_reverse_iterator acked_packets_iter_;

  // Number of times the PTO timer has fired in a row without receiving an ack.
  size_t consecutive_pto_count_;

  // True if HANDSHAKE mode has been disabled.
  bool handshake_mode_disabled_;

  // True if any ENCRYPTION_HANDSHAKE packet gets acknowledged.
  bool handshake_packet_acked_;

  // True if any 0-RTT packet gets acknowledged.
  bool zero_rtt_packet_acked_;

  // True if any 1-RTT packet gets acknowledged.
  bool one_rtt_packet_acked_;

  // The number of PTOs needed for path degrading alarm. If equals to 0, the
  // traditional path degrading mechanism will be used.
  int num_ptos_for_path_degrading_;

  // If true, do not use PING only packets for RTT measurement or congestion
  // control.
  bool ignore_pings_;

  // Whether to ignore the ack_delay in received ACKs.
  bool ignore_ack_delay_;

  // The total number of packets sent with ECT(0) or ECT(1) in each packet
  // number space over the life of the connection.
  QuicPacketCount ect0_packets_sent_[NUM_PACKET_NUMBER_SPACES] = {0, 0, 0};
  QuicPacketCount ect1_packets_sent_[NUM_PACKET_NUMBER_SPACES] = {0, 0, 0};

  // Most recent ECN codepoint counts received in an ACK frame sent by the peer.
  QuicEcnCounts peer_ack_ecn_counts_[NUM_PACKET_NUMBER_SPACES];

  std::optional<QuicTime::Delta> deferred_send_alarm_delay_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SENT_PACKET_MANAGER_H_
