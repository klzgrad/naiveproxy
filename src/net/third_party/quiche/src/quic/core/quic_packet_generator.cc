// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_generator.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_server_stats.h"

namespace quic {

QuicPacketGenerator::QuicPacketGenerator(
    QuicConnectionId server_connection_id,
    QuicFramer* framer,
    QuicRandom* random_generator,
    QuicPacketCreator::DelegateInterface* delegate)
    : delegate_(delegate),
      packet_creator_(server_connection_id, framer, random_generator, delegate),
      next_transmission_type_(NOT_RETRANSMISSION),
      flusher_attached_(false),
      random_generator_(random_generator),
      fully_pad_crypto_handshake_packets_(true) {}

QuicPacketGenerator::~QuicPacketGenerator() {}

bool QuicPacketGenerator::ConsumeRetransmittableControlFrame(
    const QuicFrame& frame) {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.ConsumeRetransmittableControlFrame(frame);
  }
  QUIC_BUG_IF(IsControlFrame(frame.type) && !GetControlFrameId(frame))
      << "Adding a control frame with no control frame id: " << frame;
  DCHECK(QuicUtils::IsRetransmittableFrame(frame.type)) << frame;
  MaybeBundleAckOpportunistically();
  if (packet_creator_.HasPendingFrames()) {
    if (packet_creator_.AddSavedFrame(frame, next_transmission_type_)) {
      // There is pending frames and current frame fits.
      return true;
    }
  }
  DCHECK(!packet_creator_.HasPendingFrames());
  if (frame.type != PING_FRAME && frame.type != CONNECTION_CLOSE_FRAME &&
      !delegate_->ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA,
                                       NOT_HANDSHAKE)) {
    // Do not check congestion window for ping or connection close frames.
    return false;
  }
  const bool success =
      packet_creator_.AddSavedFrame(frame, next_transmission_type_);
  DCHECK(success);
  return success;
}

size_t QuicPacketGenerator::ConsumeCryptoData(EncryptionLevel level,
                                              size_t write_length,
                                              QuicStreamOffset offset) {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.ConsumeCryptoData(level, write_length, offset);
  }
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to write crypto data.";
  MaybeBundleAckOpportunistically();
  // To make reasoning about crypto frames easier, we don't combine them with
  // other retransmittable frames in a single packet.
  // TODO(nharper): Once we have separate packet number spaces, everything
  // should be driven by encryption level, and we should stop flushing in this
  // spot.
  if (packet_creator_.HasPendingRetransmittableFrames()) {
    packet_creator_.FlushCurrentPacket();
  }

  size_t total_bytes_consumed = 0;

  while (total_bytes_consumed < write_length) {
    QuicFrame frame;
    if (!packet_creator_.ConsumeCryptoDataToFillCurrentPacket(
            level, write_length - total_bytes_consumed,
            offset + total_bytes_consumed, fully_pad_crypto_handshake_packets_,
            next_transmission_type_, &frame)) {
      // The only pending data in the packet is non-retransmittable frames. I'm
      // assuming here that they won't occupy so much of the packet that a
      // CRYPTO frame won't fit.
      QUIC_BUG << "Failed to ConsumeCryptoData at level " << level;
      return 0;
    }
    total_bytes_consumed += frame.crypto_frame->data_length;

    // TODO(ianswett): Move to having the creator flush itself when it's full.
    packet_creator_.FlushCurrentPacket();
  }

  // Don't allow the handshake to be bundled with other retransmittable frames.
  packet_creator_.FlushCurrentPacket();

  return total_bytes_consumed;
}

QuicConsumedData QuicPacketGenerator::ConsumeData(QuicStreamId id,
                                                  size_t write_length,
                                                  QuicStreamOffset offset,
                                                  StreamSendingState state) {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.ConsumeData(id, write_length, offset, state);
  }
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to write stream data.";
  bool has_handshake =
      QuicUtils::IsCryptoStreamId(packet_creator_.transport_version(), id);
  MaybeBundleAckOpportunistically();
  bool fin = state != NO_FIN;
  QUIC_BUG_IF(has_handshake && fin)
      << "Handshake packets should never send a fin";
  // To make reasoning about crypto frames easier, we don't combine them with
  // other retransmittable frames in a single packet.
  if (has_handshake && packet_creator_.HasPendingRetransmittableFrames()) {
    packet_creator_.FlushCurrentPacket();
  }

  size_t total_bytes_consumed = 0;
  bool fin_consumed = false;

  if (!packet_creator_.HasRoomForStreamFrame(id, offset, write_length)) {
    packet_creator_.FlushCurrentPacket();
  }

  if (!fin && (write_length == 0)) {
    QUIC_BUG << "Attempt to consume empty data without FIN.";
    return QuicConsumedData(0, false);
  }
  // We determine if we can enter the fast path before executing
  // the slow path loop.
  bool run_fast_path =
      !has_handshake && state != FIN_AND_PADDING && !HasPendingFrames() &&
      write_length - total_bytes_consumed > kMaxOutgoingPacketSize;

  while (!run_fast_path && delegate_->ShouldGeneratePacket(
                               HAS_RETRANSMITTABLE_DATA,
                               has_handshake ? IS_HANDSHAKE : NOT_HANDSHAKE)) {
    QuicFrame frame;
    bool needs_full_padding =
        has_handshake && fully_pad_crypto_handshake_packets_;

    if (!packet_creator_.ConsumeDataToFillCurrentPacket(
            id, write_length - total_bytes_consumed,
            offset + total_bytes_consumed, fin, needs_full_padding,
            next_transmission_type_, &frame)) {
      // The creator is always flushed if there's not enough room for a new
      // stream frame before ConsumeData, so ConsumeData should always succeed.
      QUIC_BUG << "Failed to ConsumeData, stream:" << id;
      return QuicConsumedData(0, false);
    }

    // A stream frame is created and added.
    size_t bytes_consumed = frame.stream_frame.data_length;
    total_bytes_consumed += bytes_consumed;
    fin_consumed = fin && total_bytes_consumed == write_length;
    if (fin_consumed && state == FIN_AND_PADDING) {
      AddRandomPadding();
    }
    DCHECK(total_bytes_consumed == write_length ||
           (bytes_consumed > 0 && packet_creator_.HasPendingFrames()));

    if (total_bytes_consumed == write_length) {
      // We're done writing the data. Exit the loop.
      // We don't make this a precondition because we could have 0 bytes of data
      // if we're simply writing a fin.
      break;
    }
    // TODO(ianswett): Move to having the creator flush itself when it's full.
    packet_creator_.FlushCurrentPacket();

    run_fast_path =
        !has_handshake && state != FIN_AND_PADDING && !HasPendingFrames() &&
        write_length - total_bytes_consumed > kMaxOutgoingPacketSize;
  }

  if (run_fast_path) {
    return ConsumeDataFastPath(id, write_length, offset, state != NO_FIN,
                               total_bytes_consumed);
  }

  // Don't allow the handshake to be bundled with other retransmittable frames.
  if (has_handshake) {
    packet_creator_.FlushCurrentPacket();
  }

  return QuicConsumedData(total_bytes_consumed, fin_consumed);
}

QuicConsumedData QuicPacketGenerator::ConsumeDataFastPath(
    QuicStreamId id,
    size_t write_length,
    QuicStreamOffset offset,
    bool fin,
    size_t total_bytes_consumed) {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.ConsumeDataFastPath(id, write_length, offset, fin,
                                               total_bytes_consumed);
  }
  DCHECK(!QuicUtils::IsCryptoStreamId(packet_creator_.transport_version(), id));

  while (total_bytes_consumed < write_length &&
         delegate_->ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA,
                                         NOT_HANDSHAKE)) {
    // Serialize and encrypt the packet.
    size_t bytes_consumed = 0;
    packet_creator_.CreateAndSerializeStreamFrame(
        id, write_length, total_bytes_consumed, offset + total_bytes_consumed,
        fin, next_transmission_type_, &bytes_consumed);
    total_bytes_consumed += bytes_consumed;
  }

  return QuicConsumedData(total_bytes_consumed,
                          fin && (total_bytes_consumed == write_length));
}

void QuicPacketGenerator::GenerateMtuDiscoveryPacket(QuicByteCount target_mtu) {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.GenerateMtuDiscoveryPacket(target_mtu);
    return;
  }
  // MTU discovery frames must be sent by themselves.
  if (!packet_creator_.CanSetMaxPacketLength()) {
    QUIC_BUG << "MTU discovery packets should only be sent when no other "
             << "frames needs to be sent.";
    return;
  }
  const QuicByteCount current_mtu = GetCurrentMaxPacketLength();

  // The MTU discovery frame is allocated on the stack, since it is going to be
  // serialized within this function.
  QuicMtuDiscoveryFrame mtu_discovery_frame;
  QuicFrame frame(mtu_discovery_frame);

  // Send the probe packet with the new length.
  SetMaxPacketLength(target_mtu);
  const bool success =
      packet_creator_.AddPaddedSavedFrame(frame, next_transmission_type_);
  packet_creator_.FlushCurrentPacket();
  // The only reason AddFrame can fail is that the packet is too full to fit in
  // a ping.  This is not possible for any sane MTU.
  DCHECK(success);

  // Reset the packet length back.
  SetMaxPacketLength(current_mtu);
}

bool QuicPacketGenerator::PacketFlusherAttached() const {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.PacketFlusherAttached();
  }
  return flusher_attached_;
}

void QuicPacketGenerator::AttachPacketFlusher() {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.AttachPacketFlusher();
    return;
  }
  flusher_attached_ = true;
  if (!write_start_packet_number_.IsInitialized()) {
    write_start_packet_number_ = packet_creator_.NextSendingPacketNumber();
  }
}

void QuicPacketGenerator::Flush() {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.Flush();
    return;
  }
  packet_creator_.FlushCurrentPacket();
  SendRemainingPendingPadding();
  flusher_attached_ = false;
  if (GetQuicFlag(FLAGS_quic_export_server_num_packets_per_write_histogram)) {
    if (!write_start_packet_number_.IsInitialized()) {
      QUIC_BUG << "write_start_packet_number is not initialized";
      return;
    }
    QUIC_SERVER_HISTOGRAM_COUNTS(
        "quic_server_num_written_packets_per_write",
        packet_creator_.NextSendingPacketNumber() - write_start_packet_number_,
        1, 200, 50, "Number of QUIC packets written per write operation");
  }
  write_start_packet_number_.Clear();
}

void QuicPacketGenerator::FlushAllQueuedFrames() {
  packet_creator_.FlushCurrentPacket();
}

bool QuicPacketGenerator::HasPendingFrames() const {
  return packet_creator_.HasPendingFrames();
}

void QuicPacketGenerator::StopSendingVersion() {
  packet_creator_.StopSendingVersion();
}

void QuicPacketGenerator::SetDiversificationNonce(
    const DiversificationNonce& nonce) {
  packet_creator_.SetDiversificationNonce(nonce);
}

QuicPacketNumber QuicPacketGenerator::packet_number() const {
  return packet_creator_.packet_number();
}

QuicByteCount QuicPacketGenerator::GetCurrentMaxPacketLength() const {
  return packet_creator_.max_packet_length();
}

void QuicPacketGenerator::SetMaxPacketLength(QuicByteCount length) {
  DCHECK(packet_creator_.CanSetMaxPacketLength());
  packet_creator_.SetMaxPacketLength(length);
}

std::unique_ptr<QuicEncryptedPacket>
QuicPacketGenerator::SerializeVersionNegotiationPacket(
    bool ietf_quic,
    bool use_length_prefix,
    const ParsedQuicVersionVector& supported_versions) {
  return packet_creator_.SerializeVersionNegotiationPacket(
      ietf_quic, use_length_prefix, supported_versions);
}

OwningSerializedPacketPointer
QuicPacketGenerator::SerializeConnectivityProbingPacket() {
  return packet_creator_.SerializeConnectivityProbingPacket();
}

OwningSerializedPacketPointer
QuicPacketGenerator::SerializePathChallengeConnectivityProbingPacket(
    QuicPathFrameBuffer* payload) {
  return packet_creator_.SerializePathChallengeConnectivityProbingPacket(
      payload);
}

OwningSerializedPacketPointer
QuicPacketGenerator::SerializePathResponseConnectivityProbingPacket(
    const QuicDeque<QuicPathFrameBuffer>& payloads,
    const bool is_padded) {
  return packet_creator_.SerializePathResponseConnectivityProbingPacket(
      payloads, is_padded);
}

void QuicPacketGenerator::ReserializeAllFrames(
    const QuicPendingRetransmission& retransmission,
    char* buffer,
    size_t buffer_len) {
  packet_creator_.ReserializeAllFrames(retransmission, buffer, buffer_len);
}

void QuicPacketGenerator::UpdatePacketNumberLength(
    QuicPacketNumber least_packet_awaited_by_peer,
    QuicPacketCount max_packets_in_flight) {
  return packet_creator_.UpdatePacketNumberLength(least_packet_awaited_by_peer,
                                                  max_packets_in_flight);
}

void QuicPacketGenerator::SkipNPacketNumbers(
    QuicPacketCount count,
    QuicPacketNumber least_packet_awaited_by_peer,
    QuicPacketCount max_packets_in_flight) {
  packet_creator_.SkipNPacketNumbers(count, least_packet_awaited_by_peer,
                                     max_packets_in_flight);
}

void QuicPacketGenerator::SetServerConnectionIdLength(uint32_t length) {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.SetServerConnectionIdLength(length);
    return;
  }
  if (length == 0) {
    packet_creator_.SetServerConnectionIdIncluded(CONNECTION_ID_ABSENT);
  } else {
    packet_creator_.SetServerConnectionIdIncluded(CONNECTION_ID_PRESENT);
  }
}

void QuicPacketGenerator::set_encryption_level(EncryptionLevel level) {
  packet_creator_.set_encryption_level(level);
}

void QuicPacketGenerator::SetEncrypter(
    EncryptionLevel level,
    std::unique_ptr<QuicEncrypter> encrypter) {
  packet_creator_.SetEncrypter(level, std::move(encrypter));
}

void QuicPacketGenerator::AddRandomPadding() {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.AddRandomPadding();
    return;
  }
  packet_creator_.AddPendingPadding(
      random_generator_->RandUint64() % kMaxNumRandomPaddingBytes + 1);
}

void QuicPacketGenerator::SendRemainingPendingPadding() {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.SendRemainingPendingPadding();
    return;
  }
  while (
      packet_creator_.pending_padding_bytes() > 0 && !HasPendingFrames() &&
      delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA, NOT_HANDSHAKE)) {
    packet_creator_.FlushCurrentPacket();
  }
}

bool QuicPacketGenerator::HasRetransmittableFrames() const {
  return packet_creator_.HasPendingRetransmittableFrames();
}

bool QuicPacketGenerator::HasPendingStreamFramesOfStream(
    QuicStreamId id) const {
  return packet_creator_.HasPendingStreamFramesOfStream(id);
}

void QuicPacketGenerator::SetTransmissionType(TransmissionType type) {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.SetTransmissionType(type);
    return;
  }
  packet_creator_.SetTransmissionTypeOfNextPackets(type);
  if (packet_creator_.can_set_transmission_type()) {
    next_transmission_type_ = type;
  }
}

void QuicPacketGenerator::SetRetryToken(QuicStringPiece retry_token) {
  packet_creator_.SetRetryToken(retry_token);
}

void QuicPacketGenerator::SetCanSetTransmissionType(
    bool can_set_transmission_type) {
  packet_creator_.set_can_set_transmission_type(can_set_transmission_type);
}

MessageStatus QuicPacketGenerator::AddMessageFrame(QuicMessageId message_id,
                                                   QuicMemSliceSpan message) {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.AddMessageFrame(message_id, message);
  }
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to add message frame.";
  MaybeBundleAckOpportunistically();
  const QuicByteCount message_length = message.total_length();
  if (message_length > GetCurrentLargestMessagePayload()) {
    return MESSAGE_STATUS_TOO_LARGE;
  }
  if (!packet_creator_.HasRoomForMessageFrame(message_length)) {
    packet_creator_.FlushCurrentPacket();
  }
  QuicMessageFrame* frame = new QuicMessageFrame(message_id, message);
  const bool success =
      packet_creator_.AddSavedFrame(QuicFrame(frame), next_transmission_type_);
  if (!success) {
    QUIC_BUG << "Failed to send message " << message_id;
    delete frame;
    return MESSAGE_STATUS_INTERNAL_ERROR;
  }
  return MESSAGE_STATUS_SUCCESS;
}

void QuicPacketGenerator::MaybeBundleAckOpportunistically() {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.MaybeBundleAckOpportunistically();
    return;
  }
  if (packet_creator_.has_ack()) {
    // Ack already queued, nothing to do.
    return;
  }
  if (!delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                       NOT_HANDSHAKE)) {
    return;
  }
  const bool flushed =
      FlushAckFrame(delegate_->MaybeBundleAckOpportunistically());
  DCHECK(flushed);
}

bool QuicPacketGenerator::FlushAckFrame(const QuicFrames& frames) {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.FlushAckFrame(frames);
  }
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to send ACK frame.";
  for (const auto& frame : frames) {
    DCHECK(frame.type == ACK_FRAME || frame.type == STOP_WAITING_FRAME);
    if (packet_creator_.HasPendingFrames()) {
      if (packet_creator_.AddSavedFrame(frame, next_transmission_type_)) {
        // There is pending frames and current frame fits.
        continue;
      }
    }
    DCHECK(!packet_creator_.HasPendingFrames());
    // There is no pending frames, consult the delegate whether a packet can be
    // generated.
    if (!delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                         NOT_HANDSHAKE)) {
      return false;
    }
    const bool success =
        packet_creator_.AddSavedFrame(frame, next_transmission_type_);
    QUIC_BUG_IF(!success) << "Failed to flush " << frame;
  }
  return true;
}

QuicPacketLength QuicPacketGenerator::GetCurrentLargestMessagePayload() const {
  return packet_creator_.GetCurrentLargestMessagePayload();
}

QuicPacketLength QuicPacketGenerator::GetGuaranteedLargestMessagePayload()
    const {
  return packet_creator_.GetGuaranteedLargestMessagePayload();
}

void QuicPacketGenerator::SetServerConnectionId(
    QuicConnectionId server_connection_id) {
  packet_creator_.SetServerConnectionId(server_connection_id);
}

void QuicPacketGenerator::SetClientConnectionId(
    QuicConnectionId client_connection_id) {
  packet_creator_.SetClientConnectionId(client_connection_id);
}

void QuicPacketGenerator::set_fully_pad_crypto_handshake_packets(
    bool new_value) {
  if (packet_creator_.combine_generator_and_creator()) {
    packet_creator_.set_fully_pad_crypto_handshake_packets(new_value);
    return;
  }
  fully_pad_crypto_handshake_packets_ = new_value;
}

bool QuicPacketGenerator::fully_pad_crypto_handshake_packets() const {
  if (packet_creator_.combine_generator_and_creator()) {
    return packet_creator_.fully_pad_crypto_handshake_packets();
  }
  return fully_pad_crypto_handshake_packets_;
}

}  // namespace quic
