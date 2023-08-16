// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_unacked_packet_map.h"

#include <cstddef>
#include <limits>
#include <type_traits>

#include "absl/container/inlined_vector.h"
#include "quiche/quic/core/quic_connection_stats.h"
#include "quiche/quic/core/quic_packet_number.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"

namespace quic {

namespace {
bool WillStreamFrameLengthSumWrapAround(QuicPacketLength lhs,
                                        QuicPacketLength rhs) {
  static_assert(
      std::is_unsigned<QuicPacketLength>::value,
      "This function assumes QuicPacketLength is an unsigned integer type.");
  return std::numeric_limits<QuicPacketLength>::max() - lhs < rhs;
}

enum QuicFrameTypeBitfield : uint32_t {
  kInvalidFrameBitfield = 0,
  kPaddingFrameBitfield = 1,
  kRstStreamFrameBitfield = 1 << 1,
  kConnectionCloseFrameBitfield = 1 << 2,
  kGoawayFrameBitfield = 1 << 3,
  kWindowUpdateFrameBitfield = 1 << 4,
  kBlockedFrameBitfield = 1 << 5,
  kStopWaitingFrameBitfield = 1 << 6,
  kPingFrameBitfield = 1 << 7,
  kCryptoFrameBitfield = 1 << 8,
  kHandshakeDoneFrameBitfield = 1 << 9,
  kStreamFrameBitfield = 1 << 10,
  kAckFrameBitfield = 1 << 11,
  kMtuDiscoveryFrameBitfield = 1 << 12,
  kNewConnectionIdFrameBitfield = 1 << 13,
  kMaxStreamsFrameBitfield = 1 << 14,
  kStreamsBlockedFrameBitfield = 1 << 15,
  kPathResponseFrameBitfield = 1 << 16,
  kPathChallengeFrameBitfield = 1 << 17,
  kStopSendingFrameBitfield = 1 << 18,
  kMessageFrameBitfield = 1 << 19,
  kNewTokenFrameBitfield = 1 << 20,
  kRetireConnectionIdFrameBitfield = 1 << 21,
  kAckFrequencyFrameBitfield = 1 << 22,
};

QuicFrameTypeBitfield GetFrameTypeBitfield(QuicFrameType type) {
  switch (type) {
    case PADDING_FRAME:
      return kPaddingFrameBitfield;
    case RST_STREAM_FRAME:
      return kRstStreamFrameBitfield;
    case CONNECTION_CLOSE_FRAME:
      return kConnectionCloseFrameBitfield;
    case GOAWAY_FRAME:
      return kGoawayFrameBitfield;
    case WINDOW_UPDATE_FRAME:
      return kWindowUpdateFrameBitfield;
    case BLOCKED_FRAME:
      return kBlockedFrameBitfield;
    case STOP_WAITING_FRAME:
      return kStopWaitingFrameBitfield;
    case PING_FRAME:
      return kPingFrameBitfield;
    case CRYPTO_FRAME:
      return kCryptoFrameBitfield;
    case HANDSHAKE_DONE_FRAME:
      return kHandshakeDoneFrameBitfield;
    case STREAM_FRAME:
      return kStreamFrameBitfield;
    case ACK_FRAME:
      return kAckFrameBitfield;
    case MTU_DISCOVERY_FRAME:
      return kMtuDiscoveryFrameBitfield;
    case NEW_CONNECTION_ID_FRAME:
      return kNewConnectionIdFrameBitfield;
    case MAX_STREAMS_FRAME:
      return kMaxStreamsFrameBitfield;
    case STREAMS_BLOCKED_FRAME:
      return kStreamsBlockedFrameBitfield;
    case PATH_RESPONSE_FRAME:
      return kPathResponseFrameBitfield;
    case PATH_CHALLENGE_FRAME:
      return kPathChallengeFrameBitfield;
    case STOP_SENDING_FRAME:
      return kStopSendingFrameBitfield;
    case MESSAGE_FRAME:
      return kMessageFrameBitfield;
    case NEW_TOKEN_FRAME:
      return kNewTokenFrameBitfield;
    case RETIRE_CONNECTION_ID_FRAME:
      return kRetireConnectionIdFrameBitfield;
    case ACK_FREQUENCY_FRAME:
      return kAckFrequencyFrameBitfield;
    case NUM_FRAME_TYPES:
      QUIC_BUG(quic_bug_10518_1) << "Unexpected frame type";
      return kInvalidFrameBitfield;
  }
  QUIC_BUG(quic_bug_10518_2) << "Unexpected frame type";
  return kInvalidFrameBitfield;
}

}  // namespace

QuicUnackedPacketMap::QuicUnackedPacketMap(Perspective perspective)
    : perspective_(perspective),
      least_unacked_(FirstSendingPacketNumber()),
      bytes_in_flight_(0),
      bytes_in_flight_per_packet_number_space_{0, 0, 0},
      packets_in_flight_(0),
      last_inflight_packet_sent_time_(QuicTime::Zero()),
      last_inflight_packets_sent_time_{
          {QuicTime::Zero()}, {QuicTime::Zero()}, {QuicTime::Zero()}},
      last_crypto_packet_sent_time_(QuicTime::Zero()),
      session_notifier_(nullptr),
      supports_multiple_packet_number_spaces_(false) {}

QuicUnackedPacketMap::~QuicUnackedPacketMap() {
  for (QuicTransmissionInfo& transmission_info : unacked_packets_) {
    DeleteFrames(&(transmission_info.retransmittable_frames));
  }
}

void QuicUnackedPacketMap::AddSentPacket(SerializedPacket* mutable_packet,
                                         TransmissionType transmission_type,
                                         QuicTime sent_time, bool set_in_flight,
                                         bool measure_rtt,
                                         QuicEcnCodepoint ecn_codepoint) {
  const SerializedPacket& packet = *mutable_packet;
  QuicPacketNumber packet_number = packet.packet_number;
  QuicPacketLength bytes_sent = packet.encrypted_length;
  QUIC_BUG_IF(quic_bug_12645_1, largest_sent_packet_.IsInitialized() &&
                                    largest_sent_packet_ >= packet_number)
      << "largest_sent_packet_: " << largest_sent_packet_
      << ", packet_number: " << packet_number;
  QUICHE_DCHECK_GE(packet_number, least_unacked_ + unacked_packets_.size());
  while (least_unacked_ + unacked_packets_.size() < packet_number) {
    unacked_packets_.push_back(QuicTransmissionInfo());
    unacked_packets_.back().state = NEVER_SENT;
  }

  const bool has_crypto_handshake = packet.has_crypto_handshake == IS_HANDSHAKE;
  QuicTransmissionInfo info(packet.encryption_level, transmission_type,
                            sent_time, bytes_sent, has_crypto_handshake,
                            packet.has_ack_frequency, ecn_codepoint);
  info.largest_acked = packet.largest_acked;
  largest_sent_largest_acked_.UpdateMax(packet.largest_acked);

  if (!measure_rtt) {
    QUIC_BUG_IF(quic_bug_12645_2, set_in_flight)
        << "Packet " << mutable_packet->packet_number << ", transmission type "
        << TransmissionTypeToString(mutable_packet->transmission_type)
        << ", retransmittable frames: "
        << QuicFramesToString(mutable_packet->retransmittable_frames)
        << ", nonretransmittable_frames: "
        << QuicFramesToString(mutable_packet->nonretransmittable_frames);
    info.state = NOT_CONTRIBUTING_RTT;
  }

  largest_sent_packet_ = packet_number;
  if (set_in_flight) {
    const PacketNumberSpace packet_number_space =
        GetPacketNumberSpace(info.encryption_level);
    bytes_in_flight_ += bytes_sent;
    bytes_in_flight_per_packet_number_space_[packet_number_space] += bytes_sent;
    ++packets_in_flight_;
    info.in_flight = true;
    largest_sent_retransmittable_packets_[packet_number_space] = packet_number;
    last_inflight_packet_sent_time_ = sent_time;
    last_inflight_packets_sent_time_[packet_number_space] = sent_time;
  }
  unacked_packets_.push_back(std::move(info));
  // Swap the retransmittable frames to avoid allocations.
  // TODO(ianswett): Could use emplace_back when Chromium can.
  if (has_crypto_handshake) {
    last_crypto_packet_sent_time_ = sent_time;
  }

  mutable_packet->retransmittable_frames.swap(
      unacked_packets_.back().retransmittable_frames);
}

void QuicUnackedPacketMap::RemoveObsoletePackets() {
  while (!unacked_packets_.empty()) {
    if (!IsPacketUseless(least_unacked_, unacked_packets_.front())) {
      break;
    }
    DeleteFrames(&unacked_packets_.front().retransmittable_frames);
    unacked_packets_.pop_front();
    ++least_unacked_;
  }
}

bool QuicUnackedPacketMap::HasRetransmittableFrames(
    QuicPacketNumber packet_number) const {
  QUICHE_DCHECK_GE(packet_number, least_unacked_);
  QUICHE_DCHECK_LT(packet_number, least_unacked_ + unacked_packets_.size());
  return HasRetransmittableFrames(
      unacked_packets_[packet_number - least_unacked_]);
}

bool QuicUnackedPacketMap::HasRetransmittableFrames(
    const QuicTransmissionInfo& info) const {
  if (!QuicUtils::IsAckable(info.state)) {
    return false;
  }

  for (const auto& frame : info.retransmittable_frames) {
    if (session_notifier_->IsFrameOutstanding(frame)) {
      return true;
    }
  }
  return false;
}

void QuicUnackedPacketMap::RemoveRetransmittability(
    QuicTransmissionInfo* info) {
  DeleteFrames(&info->retransmittable_frames);
  info->first_sent_after_loss.Clear();
}

void QuicUnackedPacketMap::RemoveRetransmittability(
    QuicPacketNumber packet_number) {
  QUICHE_DCHECK_GE(packet_number, least_unacked_);
  QUICHE_DCHECK_LT(packet_number, least_unacked_ + unacked_packets_.size());
  QuicTransmissionInfo* info =
      &unacked_packets_[packet_number - least_unacked_];
  RemoveRetransmittability(info);
}

void QuicUnackedPacketMap::IncreaseLargestAcked(
    QuicPacketNumber largest_acked) {
  QUICHE_DCHECK(!largest_acked_.IsInitialized() ||
                largest_acked_ <= largest_acked);
  largest_acked_ = largest_acked;
}

void QuicUnackedPacketMap::MaybeUpdateLargestAckedOfPacketNumberSpace(
    PacketNumberSpace packet_number_space, QuicPacketNumber packet_number) {
  largest_acked_packets_[packet_number_space].UpdateMax(packet_number);
}

bool QuicUnackedPacketMap::IsPacketUsefulForMeasuringRtt(
    QuicPacketNumber packet_number, const QuicTransmissionInfo& info) const {
  // Packet can be used for RTT measurement if it may yet be acked as the
  // largest observed packet by the receiver.
  return QuicUtils::IsAckable(info.state) &&
         (!largest_acked_.IsInitialized() || packet_number > largest_acked_) &&
         info.state != NOT_CONTRIBUTING_RTT;
}

bool QuicUnackedPacketMap::IsPacketUsefulForCongestionControl(
    const QuicTransmissionInfo& info) const {
  // Packet contributes to congestion control if it is considered inflight.
  return info.in_flight;
}

bool QuicUnackedPacketMap::IsPacketUsefulForRetransmittableData(
    const QuicTransmissionInfo& info) const {
  // Wait for 1 RTT before giving up on the lost packet.
  return info.first_sent_after_loss.IsInitialized() &&
         (!largest_acked_.IsInitialized() ||
          info.first_sent_after_loss > largest_acked_);
}

bool QuicUnackedPacketMap::IsPacketUseless(
    QuicPacketNumber packet_number, const QuicTransmissionInfo& info) const {
  return !IsPacketUsefulForMeasuringRtt(packet_number, info) &&
         !IsPacketUsefulForCongestionControl(info) &&
         !IsPacketUsefulForRetransmittableData(info);
}

bool QuicUnackedPacketMap::IsUnacked(QuicPacketNumber packet_number) const {
  if (packet_number < least_unacked_ ||
      packet_number >= least_unacked_ + unacked_packets_.size()) {
    return false;
  }
  return !IsPacketUseless(packet_number,
                          unacked_packets_[packet_number - least_unacked_]);
}

void QuicUnackedPacketMap::RemoveFromInFlight(QuicTransmissionInfo* info) {
  if (info->in_flight) {
    QUIC_BUG_IF(quic_bug_12645_3, bytes_in_flight_ < info->bytes_sent);
    QUIC_BUG_IF(quic_bug_12645_4, packets_in_flight_ == 0);
    bytes_in_flight_ -= info->bytes_sent;
    --packets_in_flight_;

    const PacketNumberSpace packet_number_space =
        GetPacketNumberSpace(info->encryption_level);
    if (bytes_in_flight_per_packet_number_space_[packet_number_space] <
        info->bytes_sent) {
      QUIC_BUG(quic_bug_10518_3)
          << "bytes_in_flight: "
          << bytes_in_flight_per_packet_number_space_[packet_number_space]
          << " is smaller than bytes_sent: " << info->bytes_sent
          << " for packet number space: "
          << PacketNumberSpaceToString(packet_number_space);
      bytes_in_flight_per_packet_number_space_[packet_number_space] = 0;
    } else {
      bytes_in_flight_per_packet_number_space_[packet_number_space] -=
          info->bytes_sent;
    }
    if (bytes_in_flight_per_packet_number_space_[packet_number_space] == 0) {
      last_inflight_packets_sent_time_[packet_number_space] = QuicTime::Zero();
    }

    info->in_flight = false;
  }
}

void QuicUnackedPacketMap::RemoveFromInFlight(QuicPacketNumber packet_number) {
  QUICHE_DCHECK_GE(packet_number, least_unacked_);
  QUICHE_DCHECK_LT(packet_number, least_unacked_ + unacked_packets_.size());
  QuicTransmissionInfo* info =
      &unacked_packets_[packet_number - least_unacked_];
  RemoveFromInFlight(info);
}

absl::InlinedVector<QuicPacketNumber, 2>
QuicUnackedPacketMap::NeuterUnencryptedPackets() {
  absl::InlinedVector<QuicPacketNumber, 2> neutered_packets;
  QuicPacketNumber packet_number = GetLeastUnacked();
  for (QuicUnackedPacketMap::iterator it = begin(); it != end();
       ++it, ++packet_number) {
    if (!it->retransmittable_frames.empty() &&
        it->encryption_level == ENCRYPTION_INITIAL) {
      QUIC_DVLOG(2) << "Neutering unencrypted packet " << packet_number;
      // Once the connection swithes to forward secure, no unencrypted packets
      // will be sent. The data has been abandoned in the cryto stream. Remove
      // it from in flight.
      RemoveFromInFlight(packet_number);
      it->state = NEUTERED;
      neutered_packets.push_back(packet_number);
      // Notify session that the data has been delivered (but do not notify
      // send algorithm).
      // TODO(b/148868195): use NotifyFramesNeutered.
      NotifyFramesAcked(*it, QuicTime::Delta::Zero(), QuicTime::Zero());
      QUICHE_DCHECK(!HasRetransmittableFrames(*it));
    }
  }
  QUICHE_DCHECK(!supports_multiple_packet_number_spaces_ ||
                last_inflight_packets_sent_time_[INITIAL_DATA] ==
                    QuicTime::Zero());
  return neutered_packets;
}

absl::InlinedVector<QuicPacketNumber, 2>
QuicUnackedPacketMap::NeuterHandshakePackets() {
  absl::InlinedVector<QuicPacketNumber, 2> neutered_packets;
  QuicPacketNumber packet_number = GetLeastUnacked();
  for (QuicUnackedPacketMap::iterator it = begin(); it != end();
       ++it, ++packet_number) {
    if (!it->retransmittable_frames.empty() &&
        GetPacketNumberSpace(it->encryption_level) == HANDSHAKE_DATA) {
      QUIC_DVLOG(2) << "Neutering handshake packet " << packet_number;
      RemoveFromInFlight(packet_number);
      // Notify session that the data has been delivered (but do not notify
      // send algorithm).
      it->state = NEUTERED;
      neutered_packets.push_back(packet_number);
      // TODO(b/148868195): use NotifyFramesNeutered.
      NotifyFramesAcked(*it, QuicTime::Delta::Zero(), QuicTime::Zero());
    }
  }
  QUICHE_DCHECK(!supports_multiple_packet_number_spaces() ||
                last_inflight_packets_sent_time_[HANDSHAKE_DATA] ==
                    QuicTime::Zero());
  return neutered_packets;
}

bool QuicUnackedPacketMap::HasInFlightPackets() const {
  return bytes_in_flight_ > 0;
}

const QuicTransmissionInfo& QuicUnackedPacketMap::GetTransmissionInfo(
    QuicPacketNumber packet_number) const {
  return unacked_packets_[packet_number - least_unacked_];
}

QuicTransmissionInfo* QuicUnackedPacketMap::GetMutableTransmissionInfo(
    QuicPacketNumber packet_number) {
  return &unacked_packets_[packet_number - least_unacked_];
}

QuicTime QuicUnackedPacketMap::GetLastInFlightPacketSentTime() const {
  return last_inflight_packet_sent_time_;
}

QuicTime QuicUnackedPacketMap::GetLastCryptoPacketSentTime() const {
  return last_crypto_packet_sent_time_;
}

size_t QuicUnackedPacketMap::GetNumUnackedPacketsDebugOnly() const {
  size_t unacked_packet_count = 0;
  QuicPacketNumber packet_number = least_unacked_;
  for (auto it = begin(); it != end(); ++it, ++packet_number) {
    if (!IsPacketUseless(packet_number, *it)) {
      ++unacked_packet_count;
    }
  }
  return unacked_packet_count;
}

bool QuicUnackedPacketMap::HasMultipleInFlightPackets() const {
  if (bytes_in_flight_ > kDefaultTCPMSS) {
    return true;
  }
  size_t num_in_flight = 0;
  for (auto it = rbegin(); it != rend(); ++it) {
    if (it->in_flight) {
      ++num_in_flight;
    }
    if (num_in_flight > 1) {
      return true;
    }
  }
  return false;
}

bool QuicUnackedPacketMap::HasPendingCryptoPackets() const {
  return session_notifier_->HasUnackedCryptoData();
}

bool QuicUnackedPacketMap::HasUnackedRetransmittableFrames() const {
  for (auto it = rbegin(); it != rend(); ++it) {
    if (it->in_flight && HasRetransmittableFrames(*it)) {
      return true;
    }
  }
  return false;
}

QuicPacketNumber QuicUnackedPacketMap::GetLeastUnacked() const {
  return least_unacked_;
}

void QuicUnackedPacketMap::SetSessionNotifier(
    SessionNotifierInterface* session_notifier) {
  session_notifier_ = session_notifier;
}

bool QuicUnackedPacketMap::NotifyFramesAcked(const QuicTransmissionInfo& info,
                                             QuicTime::Delta ack_delay,
                                             QuicTime receive_timestamp) {
  if (session_notifier_ == nullptr) {
    return false;
  }
  bool new_data_acked = false;
  for (const QuicFrame& frame : info.retransmittable_frames) {
    if (session_notifier_->OnFrameAcked(frame, ack_delay, receive_timestamp)) {
      new_data_acked = true;
    }
  }
  return new_data_acked;
}

void QuicUnackedPacketMap::NotifyFramesLost(const QuicTransmissionInfo& info,
                                            TransmissionType /*type*/) {
  for (const QuicFrame& frame : info.retransmittable_frames) {
    session_notifier_->OnFrameLost(frame);
  }
}

bool QuicUnackedPacketMap::RetransmitFrames(const QuicFrames& frames,
                                            TransmissionType type) {
  return session_notifier_->RetransmitFrames(frames, type);
}

void QuicUnackedPacketMap::MaybeAggregateAckedStreamFrame(
    const QuicTransmissionInfo& info, QuicTime::Delta ack_delay,
    QuicTime receive_timestamp) {
  if (session_notifier_ == nullptr) {
    return;
  }
  for (const auto& frame : info.retransmittable_frames) {
    // Determine whether acked stream frame can be aggregated.
    const bool can_aggregate =
        frame.type == STREAM_FRAME &&
        frame.stream_frame.stream_id == aggregated_stream_frame_.stream_id &&
        frame.stream_frame.offset == aggregated_stream_frame_.offset +
                                         aggregated_stream_frame_.data_length &&
        // We would like to increment aggregated_stream_frame_.data_length by
        // frame.stream_frame.data_length, so we need to make sure their sum is
        // representable by QuicPacketLength, which is the type of the former.
        !WillStreamFrameLengthSumWrapAround(
            aggregated_stream_frame_.data_length,
            frame.stream_frame.data_length);

    if (can_aggregate) {
      // Aggregate stream frame.
      aggregated_stream_frame_.data_length += frame.stream_frame.data_length;
      aggregated_stream_frame_.fin = frame.stream_frame.fin;
      if (aggregated_stream_frame_.fin) {
        // Notify session notifier aggregated stream frame gets acked if fin is
        // acked.
        NotifyAggregatedStreamFrameAcked(ack_delay);
      }
      continue;
    }

    NotifyAggregatedStreamFrameAcked(ack_delay);
    if (frame.type != STREAM_FRAME || frame.stream_frame.fin) {
      session_notifier_->OnFrameAcked(frame, ack_delay, receive_timestamp);
      continue;
    }

    // Delay notifying session notifier stream frame gets acked in case it can
    // be aggregated with following acked ones.
    aggregated_stream_frame_.stream_id = frame.stream_frame.stream_id;
    aggregated_stream_frame_.offset = frame.stream_frame.offset;
    aggregated_stream_frame_.data_length = frame.stream_frame.data_length;
    aggregated_stream_frame_.fin = frame.stream_frame.fin;
  }
}

void QuicUnackedPacketMap::NotifyAggregatedStreamFrameAcked(
    QuicTime::Delta ack_delay) {
  if (aggregated_stream_frame_.stream_id == static_cast<QuicStreamId>(-1) ||
      session_notifier_ == nullptr) {
    // Aggregated stream frame is empty.
    return;
  }
  // Note: there is no receive_timestamp for an aggregated stream frame.  The
  // frames that are aggregated may not have been received at the same time.
  session_notifier_->OnFrameAcked(QuicFrame(aggregated_stream_frame_),
                                  ack_delay,
                                  /*receive_timestamp=*/QuicTime::Zero());
  // Clear aggregated stream frame.
  aggregated_stream_frame_.stream_id = -1;
}

PacketNumberSpace QuicUnackedPacketMap::GetPacketNumberSpace(
    QuicPacketNumber packet_number) const {
  return GetPacketNumberSpace(
      GetTransmissionInfo(packet_number).encryption_level);
}

PacketNumberSpace QuicUnackedPacketMap::GetPacketNumberSpace(
    EncryptionLevel encryption_level) const {
  if (supports_multiple_packet_number_spaces_) {
    return QuicUtils::GetPacketNumberSpace(encryption_level);
  }
  if (perspective_ == Perspective::IS_CLIENT) {
    return encryption_level == ENCRYPTION_INITIAL ? HANDSHAKE_DATA
                                                  : APPLICATION_DATA;
  }
  return encryption_level == ENCRYPTION_FORWARD_SECURE ? APPLICATION_DATA
                                                       : HANDSHAKE_DATA;
}

QuicPacketNumber QuicUnackedPacketMap::GetLargestAckedOfPacketNumberSpace(
    PacketNumberSpace packet_number_space) const {
  if (packet_number_space >= NUM_PACKET_NUMBER_SPACES) {
    QUIC_BUG(quic_bug_10518_4)
        << "Invalid packet number space: " << packet_number_space;
    return QuicPacketNumber();
  }
  return largest_acked_packets_[packet_number_space];
}

QuicTime QuicUnackedPacketMap::GetLastInFlightPacketSentTime(
    PacketNumberSpace packet_number_space) const {
  if (packet_number_space >= NUM_PACKET_NUMBER_SPACES) {
    QUIC_BUG(quic_bug_10518_5)
        << "Invalid packet number space: " << packet_number_space;
    return QuicTime::Zero();
  }
  return last_inflight_packets_sent_time_[packet_number_space];
}

QuicPacketNumber
QuicUnackedPacketMap::GetLargestSentRetransmittableOfPacketNumberSpace(
    PacketNumberSpace packet_number_space) const {
  if (packet_number_space >= NUM_PACKET_NUMBER_SPACES) {
    QUIC_BUG(quic_bug_10518_6)
        << "Invalid packet number space: " << packet_number_space;
    return QuicPacketNumber();
  }
  return largest_sent_retransmittable_packets_[packet_number_space];
}

const QuicTransmissionInfo*
QuicUnackedPacketMap::GetFirstInFlightTransmissionInfo() const {
  QUICHE_DCHECK(HasInFlightPackets());
  for (auto it = begin(); it != end(); ++it) {
    if (it->in_flight) {
      return &(*it);
    }
  }
  QUICHE_DCHECK(false);
  return nullptr;
}

const QuicTransmissionInfo*
QuicUnackedPacketMap::GetFirstInFlightTransmissionInfoOfSpace(
    PacketNumberSpace packet_number_space) const {
  // TODO(fayang): Optimize this part if arm 1st PTO with first in flight sent
  // time works.
  for (auto it = begin(); it != end(); ++it) {
    if (it->in_flight &&
        GetPacketNumberSpace(it->encryption_level) == packet_number_space) {
      return &(*it);
    }
  }
  return nullptr;
}

void QuicUnackedPacketMap::EnableMultiplePacketNumberSpacesSupport() {
  if (supports_multiple_packet_number_spaces_) {
    QUIC_BUG(quic_bug_10518_7)
        << "Multiple packet number spaces has already been enabled";
    return;
  }
  if (largest_sent_packet_.IsInitialized()) {
    QUIC_BUG(quic_bug_10518_8)
        << "Try to enable multiple packet number spaces support after any "
           "packet has been sent.";
    return;
  }

  supports_multiple_packet_number_spaces_ = true;
}

int32_t QuicUnackedPacketMap::GetLastPacketContent() const {
  if (empty()) {
    // Use -1 to distinguish with packets with no retransmittable frames nor
    // acks.
    return -1;
  }
  int32_t content = 0;
  const QuicTransmissionInfo& last_packet = unacked_packets_.back();
  for (const auto& frame : last_packet.retransmittable_frames) {
    content |= GetFrameTypeBitfield(frame.type);
  }
  if (last_packet.largest_acked.IsInitialized()) {
    content |= GetFrameTypeBitfield(ACK_FRAME);
  }
  return content;
}

}  // namespace quic
