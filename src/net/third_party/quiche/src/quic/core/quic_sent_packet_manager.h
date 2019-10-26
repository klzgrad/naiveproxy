// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_SENT_PACKET_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_SENT_PACKET_MANAGER_H_

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/core/congestion_control/pacing_sender.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/rtt_stats.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/send_algorithm_interface.h"
#include "net/third_party/quiche/src/quic/core/congestion_control/uber_loss_algorithm.h"
#include "net/third_party/quiche/src/quic/core/proto/cached_network_parameters_proto.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_pending_retransmission.h"
#include "net/third_party/quiche/src/quic/core/quic_sustained_bandwidth_recorder.h"
#include "net/third_party/quiche/src/quic/core/quic_transmission_info.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_unacked_packet_map.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

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
class QUIC_EXPORT_PRIVATE QuicSentPacketManager {
 public:
  // Interface which gets callbacks from the QuicSentPacketManager at
  // interesting points.  Implementations must not mutate the state of
  // the packet manager or connection as a result of these callbacks.
  class QUIC_EXPORT_PRIVATE DebugDelegate {
   public:
    virtual ~DebugDelegate() {}

    // Called when a spurious retransmission is detected.
    virtual void OnSpuriousPacketRetransmission(
        TransmissionType /*transmission_type*/,
        QuicByteCount /*byte_size*/) {}

    virtual void OnIncomingAck(QuicPacketNumber /*ack_packet_number*/,
                               const QuicAckFrame& /*ack_frame*/,
                               QuicTime /*ack_receive_time*/,
                               QuicPacketNumber /*largest_observed*/,
                               bool /*rtt_updated*/,
                               QuicPacketNumber /*least_unacked_sent_packet*/) {
    }

    virtual void OnPacketLoss(QuicPacketNumber /*lost_packet_number*/,
                              TransmissionType /*transmission_type*/,
                              QuicTime /*detection_time*/) {}

    virtual void OnApplicationLimited() {}

    virtual void OnAdjustNetworkParameters(QuicBandwidth /*bandwidth*/,
                                           QuicTime::Delta /*rtt*/,
                                           QuicByteCount /*old_cwnd*/,
                                           QuicByteCount /*new_cwnd*/) {}
  };

  // Interface which gets callbacks from the QuicSentPacketManager when
  // network-related state changes. Implementations must not mutate the
  // state of the packet manager as a result of these callbacks.
  class QUIC_EXPORT_PRIVATE NetworkChangeVisitor {
   public:
    virtual ~NetworkChangeVisitor() {}

    // Called when congestion window or RTT may have changed.
    virtual void OnCongestionChange() = 0;

    // Called when the Path MTU may have increased.
    virtual void OnPathMtuIncreased(QuicPacketLength packet_size) = 0;
  };

  // The retransmission timer is a single timer which switches modes depending
  // upon connection state.
  enum RetransmissionTimeoutMode {
    // A conventional TCP style RTO.
    RTO_MODE,
    // A tail loss probe.  By default, QUIC sends up to two before RTOing.
    TLP_MODE,
    // Retransmission of handshake packets prior to handshake completion.
    HANDSHAKE_MODE,
    // Re-invoke the loss detection when a packet is not acked before the
    // loss detection algorithm expects.
    LOSS_MODE,
    // A probe timeout. At least one probe packet must be sent when timer
    // expires.
    PTO_MODE,
  };

  QuicSentPacketManager(Perspective perspective,
                        const QuicClock* clock,
                        QuicRandom* random,
                        QuicConnectionStats* stats,
                        CongestionControlType congestion_control_type,
                        LossDetectionType loss_type);
  QuicSentPacketManager(const QuicSentPacketManager&) = delete;
  QuicSentPacketManager& operator=(const QuicSentPacketManager&) = delete;
  virtual ~QuicSentPacketManager();

  virtual void SetFromConfig(const QuicConfig& config);

  // Pass the CachedNetworkParameters to the send algorithm.
  void ResumeConnectionState(
      const CachedNetworkParameters& cached_network_params,
      bool max_bandwidth_resumption);

  void SetMaxPacingRate(QuicBandwidth max_pacing_rate) {
    pacing_sender_.set_max_pacing_rate(max_pacing_rate);
  }

  QuicBandwidth MaxPacingRate() const {
    return pacing_sender_.max_pacing_rate();
  }

  // Set handshake_confirmed_ to true and neuter packets in HANDSHAKE packet
  // number space.
  void SetHandshakeConfirmed();

  // Requests retransmission of all unacked packets of |retransmission_type|.
  // The behavior of this method depends on the value of |retransmission_type|:
  // ALL_UNACKED_RETRANSMISSION - All unacked packets will be retransmitted.
  // This can happen, for example, after a version negotiation packet has been
  // received and all packets needs to be retransmitted with the new version.
  // ALL_INITIAL_RETRANSMISSION - Only initially encrypted packets will be
  // retransmitted. This can happen, for example, when a CHLO has been rejected
  // and the previously encrypted data needs to be encrypted with a new key.
  void RetransmitUnackedPackets(TransmissionType retransmission_type);

  // Notify the sent packet manager of an external network measurement or
  // prediction for either |bandwidth| or |rtt|; either can be empty.
  void AdjustNetworkParameters(QuicBandwidth bandwidth,
                               QuicTime::Delta rtt,
                               bool allow_cwnd_to_decrease);

  // Retransmits the oldest pending packet there is still a tail loss probe
  // pending.  Invoked after OnRetransmissionTimeout.
  bool MaybeRetransmitTailLossProbe();

  // Retransmits the oldest pending packet.
  bool MaybeRetransmitOldestPacket(TransmissionType type);

  // Removes the retransmittable frames from all unencrypted packets to ensure
  // they don't get retransmitted.
  // TODO(fayang): Consider replace this function with NeuterHandshakePackets.
  void NeuterUnencryptedPackets();

  // Returns true if there are pending retransmissions.
  // Not const because retransmissions may be cancelled before returning.
  bool HasPendingRetransmissions() const {
    return !pending_retransmissions_.empty();
  }

  // Retrieves the next pending retransmission.  You must ensure that
  // there are pending retransmissions prior to calling this function.
  QuicPendingRetransmission NextPendingRetransmission();

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
  // the number of bytes sent and if they were retransmitted.  Returns true if
  // the sender should reset the retransmission timer.
  bool OnPacketSent(SerializedPacket* serialized_packet,
                    QuicPacketNumber original_packet_number,
                    QuicTime sent_time,
                    TransmissionType transmission_type,
                    HasRetransmittableData has_retransmittable_data);

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

  const RttStats* GetRttStats() const { return &rtt_stats_; }

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

  // No longer retransmit data for |stream_id|.
  void CancelRetransmissionsForStream(QuicStreamId stream_id);

  // Called when peer address changes and the connection migrates.
  void OnConnectionMigration(AddressChangeType type);

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
                          EncryptionLevel ack_decrypted_level);

  // Called to enable/disable letting session decide what to write.
  void SetSessionDecideWhatToWrite(bool session_decides_what_to_write);

  void EnableMultiplePacketNumberSpacesSupport();

  void SetDebugDelegate(DebugDelegate* debug_delegate);

  void SetPacingAlarmGranularity(QuicTime::Delta alarm_granularity) {
    pacing_sender_.set_alarm_granularity(alarm_granularity);
  }

  QuicPacketNumber GetLargestObserved() const {
    return unacked_packets_.largest_acked();
  }

  QuicPacketNumber GetLargestAckedPacket(
      EncryptionLevel decrypted_packet_level) const;

  QuicPacketNumber GetLargestSentPacket() const {
    return unacked_packets_.largest_sent_packet();
  }

  QuicPacketNumber GetLargestSentPacket(
      EncryptionLevel decrypted_packet_level) const;

  QuicPacketNumber GetLargestPacketPeerKnowsIsAcked(
      EncryptionLevel decrypted_packet_level) const;

  void SetNetworkChangeVisitor(NetworkChangeVisitor* visitor) {
    DCHECK(!network_change_visitor_);
    DCHECK(visitor);
    network_change_visitor_ = visitor;
  }

  bool InSlowStart() const { return send_algorithm_->InSlowStart(); }

  size_t GetConsecutiveRtoCount() const { return consecutive_rto_count_; }

  size_t GetConsecutiveTlpCount() const { return consecutive_tlp_count_; }

  size_t GetConsecutivePtoCount() const { return consecutive_pto_count_; }

  void OnApplicationLimited();

  const SendAlgorithmInterface* GetSendAlgorithm() const {
    return send_algorithm_.get();
  }

  void SetSessionNotifier(SessionNotifierInterface* session_notifier) {
    unacked_packets_.SetSessionNotifier(session_notifier);
  }

  QuicTime GetNextReleaseTime() const;

  QuicPacketCount initial_congestion_window() const {
    return initial_congestion_window_;
  }

  QuicPacketNumber largest_packet_peer_knows_is_acked() const {
    DCHECK(!supports_multiple_packet_number_spaces());
    return largest_packet_peer_knows_is_acked_;
  }

  bool handshake_confirmed() const { return handshake_confirmed_; }

  bool session_decides_what_to_write() const {
    return unacked_packets_.session_decides_what_to_write();
  }

  size_t pending_timer_transmission_count() const {
    return pending_timer_transmission_count_;
  }

  QuicTime::Delta peer_max_ack_delay() const { return peer_max_ack_delay_; }

  void set_peer_max_ack_delay(QuicTime::Delta peer_max_ack_delay) {
    // The delayed ack time should never be more than one half the min RTO time.
    DCHECK_LE(peer_max_ack_delay, (min_rto_timeout_ * 0.5));
    peer_max_ack_delay_ = peer_max_ack_delay;
  }

  const QuicUnackedPacketMap& unacked_packets() const {
    return unacked_packets_;
  }

  // Sets the send algorithm to the given congestion control type and points the
  // pacing sender at |send_algorithm_|. Can be called any number of times.
  void SetSendAlgorithm(CongestionControlType congestion_control_type);

  // Sets the send algorithm to |send_algorithm| and points the pacing sender at
  // |send_algorithm_|. Takes ownership of |send_algorithm|. Can be called any
  // number of times.
  // Setting the send algorithm once the connection is underway is dangerous.
  void SetSendAlgorithm(SendAlgorithmInterface* send_algorithm);

  // Sends up to max_probe_packets_per_pto_ probe packets.
  void MaybeSendProbePackets();

  // Called to adjust pending_timer_transmission_count_ accordingly.
  void AdjustPendingTimerTransmissions();

  // Called to disable HANDSHAKE_MODE, and only PTO and LOSS modes are used.
  void DisableHandshakeMode();

  bool supports_multiple_packet_number_spaces() const {
    return unacked_packets_.supports_multiple_packet_number_spaces();
  }

  bool ignore_tlpr_if_no_pending_stream_data() const {
    return ignore_tlpr_if_no_pending_stream_data_;
  }

  bool fix_rto_retransmission() const { return fix_rto_retransmission_; }

  bool pto_enabled() const { return pto_enabled_; }

  bool handshake_mode_disabled() const { return handshake_mode_disabled_; }

 private:
  friend class test::QuicConnectionPeer;
  friend class test::QuicSentPacketManagerPeer;

  typedef QuicLinkedHashMap<QuicPacketNumber,
                            TransmissionType,
                            QuicPacketNumberHash>
      PendingRetransmissionMap;

  // Returns the current retransmission mode.
  RetransmissionTimeoutMode GetRetransmissionMode() const;

  // Retransmits all crypto stream packets.
  void RetransmitCryptoPackets();

  // Retransmits two packets for an RTO and removes any non-retransmittable
  // packets from flight.
  void RetransmitRtoPackets();

  // Returns the timeout for retransmitting crypto handshake packets.
  const QuicTime::Delta GetCryptoRetransmissionDelay() const;

  // Returns the timeout for a new tail loss probe. |consecutive_tlp_count| is
  // the number of consecutive tail loss probes that have already been sent.
  const QuicTime::Delta GetTailLossProbeDelay(
      size_t consecutive_tlp_count) const;

  // Calls GetTailLossProbeDelay() with values from the current state of this
  // packet manager as its params.
  const QuicTime::Delta GetTailLossProbeDelay() const {
    return GetTailLossProbeDelay(consecutive_tlp_count_);
  }

  // Returns the retransmission timeout, after which a full RTO occurs.
  // |consecutive_rto_count| is the number of consecutive RTOs that have already
  // occurred.
  const QuicTime::Delta GetRetransmissionDelay(
      size_t consecutive_rto_count) const;

  // Calls GetRetransmissionDelay() with values from the current state of this
  // packet manager as its params.
  const QuicTime::Delta GetRetransmissionDelay() const {
    return GetRetransmissionDelay(consecutive_rto_count_);
  }

  // Returns the probe timeout.
  const QuicTime::Delta GetProbeTimeoutDelay() const;

  // Returns the newest transmission associated with a packet.
  QuicPacketNumber GetNewestRetransmission(
      QuicPacketNumber packet_number,
      const QuicTransmissionInfo& transmission_info) const;

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
                                  QuicTime event_time);

  // Removes the retransmittability and in flight properties from the packet at
  // |info| due to receipt by the peer.
  void MarkPacketHandled(QuicPacketNumber packet_number,
                         QuicTransmissionInfo* info,
                         QuicTime::Delta ack_delay_time,
                         QuicTime receive_timestamp);

  // Request that |packet_number| be retransmitted after the other pending
  // retransmissions.  Does not add it to the retransmissions if it's already
  // a pending retransmission.
  void MarkForRetransmission(QuicPacketNumber packet_number,
                             TransmissionType transmission_type);

  // Performs whatever work is need to retransmit the data correctly, either
  // by retransmitting the frames directly or by notifying that the frames
  // are lost.
  void HandleRetransmission(TransmissionType transmission_type,
                            QuicTransmissionInfo* transmission_info);

  // Called after packets have been marked handled with last received ack frame.
  void PostProcessNewlyAckedPackets(QuicPacketNumber ack_packet_number,
                                    const QuicAckFrame& ack_frame,
                                    QuicTime ack_receive_time,
                                    bool rtt_updated,
                                    QuicByteCount prior_bytes_in_flight);

  // Notify observers that packet with QuicTransmissionInfo |info| is a spurious
  // retransmission. It is caller's responsibility to guarantee the packet with
  // QuicTransmissionInfo |info| is a spurious retransmission before calling
  // this function.
  void RecordOneSpuriousRetransmission(const QuicTransmissionInfo& info);

  // Notify observers about spurious retransmits of packet with
  // QuicTransmissionInfo |info|.
  void RecordSpuriousRetransmissions(const QuicTransmissionInfo& info,
                                     QuicPacketNumber acked_packet_number);

  // Sets the initial RTT of the connection.
  void SetInitialRtt(QuicTime::Delta rtt);

  // Should only be called from constructor.
  LossDetectionInterface* GetInitialLossAlgorithm();

  // Called when handshake is confirmed to remove the retransmittable frames
  // from all packets of HANDSHAKE_DATA packet number space to ensure they don't
  // get retransmitted and will eventually be removed from unacked packets map.
  // Please note, this only applies to QUIC Crypto and needs to be changed when
  // switches to IETF QUIC with QUIC TLS.
  void NeuterHandshakePackets();

  // Newly serialized retransmittable packets are added to this map, which
  // contains owning pointers to any contained frames.  If a packet is
  // retransmitted, this map will contain entries for both the old and the new
  // packet. The old packet's retransmittable frames entry will be nullptr,
  // while the new packet's entry will contain the frames to retransmit.
  // If the old packet is acked before the new packet, then the old entry will
  // be removed from the map and the new entry's retransmittable frames will be
  // set to nullptr.
  QuicUnackedPacketMap unacked_packets_;

  // Pending retransmissions which have not been packetized and sent yet.
  PendingRetransmissionMap pending_retransmissions_;

  const QuicClock* clock_;
  QuicRandom* random_;
  QuicConnectionStats* stats_;

  DebugDelegate* debug_delegate_;
  NetworkChangeVisitor* network_change_visitor_;
  QuicPacketCount initial_congestion_window_;
  RttStats rtt_stats_;
  std::unique_ptr<SendAlgorithmInterface> send_algorithm_;
  // Not owned. Always points to |general_loss_algorithm_| outside of tests.
  LossDetectionInterface* loss_algorithm_;
  UberLossAlgorithm uber_loss_algorithm_;

  // Tracks the first RTO packet.  If any packet before that packet gets acked,
  // it indicates the RTO was spurious and should be reversed(F-RTO).
  QuicPacketNumber first_rto_transmission_;
  // Number of times the RTO timer has fired in a row without receiving an ack.
  size_t consecutive_rto_count_;
  // Number of times the tail loss probe has been sent.
  size_t consecutive_tlp_count_;
  // Number of times the crypto handshake has been retransmitted.
  size_t consecutive_crypto_retransmission_count_;
  // Number of pending transmissions of TLP, RTO, or crypto packets.
  size_t pending_timer_transmission_count_;
  // Maximum number of tail loss probes to send before firing an RTO.
  size_t max_tail_loss_probes_;
  // Maximum number of packets to send upon RTO.
  QuicPacketCount max_rto_packets_;
  // If true, send the TLP at 0.5 RTT.
  bool enable_half_rtt_tail_loss_probe_;
  bool using_pacing_;
  // If true, use the new RTO with loss based CWND reduction instead of the send
  // algorithms's OnRetransmissionTimeout to reduce the congestion window.
  bool use_new_rto_;
  // If true, use a more conservative handshake retransmission policy.
  bool conservative_handshake_retransmits_;
  // The minimum TLP timeout.
  QuicTime::Delta min_tlp_timeout_;
  // The minimum RTO.
  QuicTime::Delta min_rto_timeout_;
  // Whether to use IETF style TLP that includes the max ack delay.
  bool ietf_style_tlp_;
  // IETF style TLP, but with a 2x multiplier instead of 1.5x.
  bool ietf_style_2x_tlp_;

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

  // Set to true after the crypto handshake has successfully completed. After
  // this is true we no longer use HANDSHAKE_MODE, and further frames sent on
  // the crypto stream (i.e. SCUP messages) are treated like normal
  // retransmittable frames.
  bool handshake_confirmed_;

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

  // The maximum ACK delay time that the peer uses. Initialized to be the
  // same as local_max_ack_delay_, may be changed via transport parameter
  // negotiation.
  QuicTime::Delta peer_max_ack_delay_;

  // Latest received ack frame.
  QuicAckFrame last_ack_frame_;

  // Record whether RTT gets updated by last largest acked..
  bool rtt_updated_;

  // A reverse iterator of last_ack_frame_.packets. This is reset in
  // OnAckRangeStart, and gradually moves in OnAckRange..
  PacketNumberQueue::const_reverse_iterator acked_packets_iter_;

  // Indicates whether PTO mode has been enabled. PTO mode unifies TLP and RTO
  // modes.
  bool pto_enabled_;

  // Maximum number of probes to send when PTO fires.
  size_t max_probe_packets_per_pto_;

  // Number of times the PTO timer has fired in a row without receiving an ack.
  size_t consecutive_pto_count_;

  // Latched value of quic_loss_removes_from_inflight.
  const bool loss_removes_from_inflight_;

  // Latched value of quic_ignore_tlpr_if_no_pending_stream_data.
  const bool ignore_tlpr_if_no_pending_stream_data_;

  // Latched value of quic_fix_rto_retransmission3 and
  // session_decides_what_to_write.
  bool fix_rto_retransmission_;

  // True if HANDSHAKE mode has been disabled.
  bool handshake_mode_disabled_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SENT_PACKET_MANAGER_H_
