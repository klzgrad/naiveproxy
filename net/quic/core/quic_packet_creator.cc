// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_packet_creator.h"

#include <algorithm>
#include <cstdint>

#include "base/macros.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/core/quic_data_writer.h"
#include "net/quic/core/quic_utils.h"
#include "net/quic/platform/api/quic_aligned.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "net/quic/platform/api/quic_logging.h"
#include "net/quic/platform/api/quic_ptr_util.h"
#include "net/quic/platform/api/quic_string_piece.h"

using std::string;

// If true, enforce that QUIC CHLOs fit in one packet.
bool FLAGS_quic_enforce_single_packet_chlo = true;

namespace net {

#define ENDPOINT \
  (framer_->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicPacketCreator::QuicPacketCreator(QuicConnectionId connection_id,
                                     QuicFramer* framer,
                                     QuicBufferAllocator* buffer_allocator,
                                     DelegateInterface* delegate)
    : delegate_(delegate),
      debug_delegate_(nullptr),
      framer_(framer),
      buffer_allocator_(buffer_allocator),
      send_version_in_packet_(framer->perspective() == Perspective::IS_CLIENT),
      have_diversification_nonce_(false),
      max_packet_length_(0),
      connection_id_length_(PACKET_8BYTE_CONNECTION_ID),
      packet_size_(0),
      connection_id_(connection_id),
      packet_(0, PACKET_1BYTE_PACKET_NUMBER, nullptr, 0, false, false),
      pending_padding_bytes_(0),
      needs_full_padding_(false) {
  SetMaxPacketLength(kDefaultMaxPacketSize);
}

QuicPacketCreator::~QuicPacketCreator() {
  DeleteFrames(&packet_.retransmittable_frames);
}

void QuicPacketCreator::SetEncrypter(EncryptionLevel level,
                                     QuicEncrypter* encrypter) {
  framer_->SetEncrypter(level, encrypter);
  max_plaintext_size_ = framer_->GetMaxPlaintextSize(max_packet_length_);
}

bool QuicPacketCreator::CanSetMaxPacketLength() const {
  // |max_packet_length_| should not be changed mid-packet.
  return queued_frames_.empty();
}

void QuicPacketCreator::SetMaxPacketLength(QuicByteCount length) {
  DCHECK(CanSetMaxPacketLength());

  // Avoid recomputing |max_plaintext_size_| if the length does not actually
  // change.
  if (length == max_packet_length_) {
    return;
  }

  max_packet_length_ = length;
  max_plaintext_size_ = framer_->GetMaxPlaintextSize(max_packet_length_);
}

// Stops serializing version of the protocol in packets sent after this call.
// A packet that is already open might send kQuicVersionSize bytes less than the
// maximum packet size if we stop sending version before it is serialized.
void QuicPacketCreator::StopSendingVersion() {
  DCHECK(send_version_in_packet_);
  send_version_in_packet_ = false;
  if (packet_size_ > 0) {
    DCHECK_LT(kQuicVersionSize, packet_size_);
    packet_size_ -= kQuicVersionSize;
  }
}

void QuicPacketCreator::SetDiversificationNonce(
    const DiversificationNonce& nonce) {
  DCHECK(!have_diversification_nonce_);
  have_diversification_nonce_ = true;
  diversification_nonce_ = nonce;
}

void QuicPacketCreator::UpdatePacketNumberLength(
    QuicPacketNumber least_packet_awaited_by_peer,
    QuicPacketCount max_packets_in_flight) {
  if (!queued_frames_.empty()) {
    // Don't change creator state if there are frames queued.
    QUIC_BUG << "Called UpdatePacketNumberLength with " << queued_frames_.size()
             << " queued_frames.  First frame type:"
             << queued_frames_.front().type
             << " last frame type:" << queued_frames_.back().type;
    return;
  }

  DCHECK_LE(least_packet_awaited_by_peer, packet_.packet_number + 1);
  const QuicPacketNumber current_delta =
      packet_.packet_number + 1 - least_packet_awaited_by_peer;
  const uint64_t delta = std::max(current_delta, max_packets_in_flight);
  packet_.packet_number_length = QuicFramer::GetMinPacketNumberLength(
      framer_->transport_version(), delta * 4);
}

bool QuicPacketCreator::ConsumeData(QuicStreamId id,
                                    QuicIOVector iov,
                                    size_t iov_offset,
                                    QuicStreamOffset offset,
                                    bool fin,
                                    bool needs_full_padding,
                                    QuicFrame* frame) {
  if (!HasRoomForStreamFrame(id, offset)) {
    return false;
  }
  CreateStreamFrame(id, iov, iov_offset, offset, fin, frame);
  // Explicitly disallow multi-packet CHLOs.
  if (FLAGS_quic_enforce_single_packet_chlo &&
      StreamFrameStartsWithChlo(iov, iov_offset, *frame->stream_frame) &&
      frame->stream_frame->data_length < iov.total_length) {
    const string error_details = "Client hello won't fit in a single packet.";
    QUIC_BUG << error_details << " Constructed stream frame length: "
             << frame->stream_frame->data_length
             << " CHLO length: " << iov.total_length;
    delegate_->OnUnrecoverableError(QUIC_CRYPTO_CHLO_TOO_LARGE, error_details,
                                    ConnectionCloseSource::FROM_SELF);
    delete frame->stream_frame;
    return false;
  }
  if (!AddFrame(*frame, /*save_retransmittable_frames=*/true)) {
    // Fails if we try to write unencrypted stream data.
    delete frame->stream_frame;
    return false;
  }
  if (needs_full_padding) {
    needs_full_padding_ = true;
  }

  return true;
}

bool QuicPacketCreator::HasRoomForStreamFrame(QuicStreamId id,
                                              QuicStreamOffset offset) {
  return BytesFree() > QuicFramer::GetMinStreamFrameSize(
                           framer_->transport_version(), id, offset, true);
}

// static
size_t QuicPacketCreator::StreamFramePacketOverhead(
    QuicTransportVersion version,
    QuicConnectionIdLength connection_id_length,
    bool include_version,
    bool include_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    QuicStreamOffset offset) {
  return GetPacketHeaderSize(version, connection_id_length, include_version,
                             include_diversification_nonce,
                             packet_number_length) +
         // Assumes this is a stream with a single lone packet.
         QuicFramer::GetMinStreamFrameSize(version, 1u, offset, true);
}

void QuicPacketCreator::CreateStreamFrame(QuicStreamId id,
                                          QuicIOVector iov,
                                          size_t iov_offset,
                                          QuicStreamOffset offset,
                                          bool fin,
                                          QuicFrame* frame) {
  DCHECK_GT(max_packet_length_,
            StreamFramePacketOverhead(framer_->transport_version(),
                                      connection_id_length_, kIncludeVersion,
                                      IncludeNonceInPublicHeader(),
                                      PACKET_6BYTE_PACKET_NUMBER, offset));

  QUIC_BUG_IF(!HasRoomForStreamFrame(id, offset))
      << "No room for Stream frame, BytesFree: " << BytesFree()
      << " MinStreamFrameSize: "
      << QuicFramer::GetMinStreamFrameSize(framer_->transport_version(), id,
                                           offset, true);

  if (iov_offset == iov.total_length) {
    QUIC_BUG_IF(!fin) << "Creating a stream frame with no data or fin.";
    // Create a new packet for the fin, if necessary.
    *frame =
        QuicFrame(new QuicStreamFrame(id, true, offset, QuicStringPiece()));
    return;
  }

  const size_t data_size = iov.total_length - iov_offset;
  size_t min_frame_size = QuicFramer::GetMinStreamFrameSize(
      framer_->transport_version(), id, offset,
      /* last_frame_in_packet= */ true);
  size_t bytes_consumed =
      std::min<size_t>(BytesFree() - min_frame_size, data_size);

  bool set_fin = fin && bytes_consumed == data_size;  // Last frame.
  if (framer_->HasDataProducer()) {
    *frame =
        QuicFrame(new QuicStreamFrame(id, set_fin, offset, bytes_consumed));
    return;
  }
  UniqueStreamBuffer buffer =
      NewStreamBuffer(buffer_allocator_, bytes_consumed);
  QuicUtils::CopyToBuffer(iov, iov_offset, bytes_consumed, buffer.get());
  *frame = QuicFrame(new QuicStreamFrame(id, set_fin, offset, bytes_consumed,
                                         std::move(buffer)));
}

void QuicPacketCreator::ReserializeAllFrames(
    const QuicPendingRetransmission& retransmission,
    char* buffer,
    size_t buffer_len) {
  DCHECK(queued_frames_.empty());
  DCHECK_EQ(0, packet_.num_padding_bytes);
  QUIC_BUG_IF(retransmission.retransmittable_frames.empty())
      << "Attempt to serialize empty packet";
  const EncryptionLevel default_encryption_level = packet_.encryption_level;

  // Temporarily set the packet number length and change the encryption level.
  packet_.packet_number_length = retransmission.packet_number_length;
  if (retransmission.num_padding_bytes == -1) {
    // Only retransmit padding when original packet needs full padding. Padding
    // from pending_padding_bytes_ are not retransmitted.
    needs_full_padding_ = true;
  }
  // Only preserve the original encryption level if it's a handshake packet or
  // if we haven't gone forward secure.
  if (retransmission.has_crypto_handshake ||
      packet_.encryption_level != ENCRYPTION_FORWARD_SECURE) {
    packet_.encryption_level = retransmission.encryption_level;
  }

  // Serialize the packet and restore packet number length state.
  for (const QuicFrame& frame : retransmission.retransmittable_frames) {
    bool success = AddFrame(frame, false);
    QUIC_BUG_IF(!success) << " Failed to add frame of type:" << frame.type
                          << " num_frames:"
                          << retransmission.retransmittable_frames.size()
                          << " retransmission.packet_number_length:"
                          << retransmission.packet_number_length
                          << " packet_.packet_number_length:"
                          << packet_.packet_number_length;
  }
  SerializePacket(buffer, buffer_len);
  packet_.original_packet_number = retransmission.packet_number;
  packet_.transmission_type = retransmission.transmission_type;
  OnSerializedPacket();
  // Restore old values.
  packet_.encryption_level = default_encryption_level;
}

void QuicPacketCreator::Flush() {
  if (!HasPendingFrames() && pending_padding_bytes_ == 0) {
    return;
  }

  QUIC_CACHELINE_ALIGNED char serialized_packet_buffer[kMaxPacketSize];
  SerializePacket(serialized_packet_buffer, kMaxPacketSize);
  OnSerializedPacket();
}

void QuicPacketCreator::OnSerializedPacket() {
  if (packet_.encrypted_buffer == nullptr) {
    const string error_details = "Failed to SerializePacket.";
    QUIC_BUG << error_details;
    delegate_->OnUnrecoverableError(QUIC_FAILED_TO_SERIALIZE_PACKET,
                                    error_details,
                                    ConnectionCloseSource::FROM_SELF);
    return;
  }

  SerializedPacket packet(std::move(packet_));
  ClearPacket();
  delegate_->OnSerializedPacket(&packet);
}

void QuicPacketCreator::ClearPacket() {
  packet_.has_ack = false;
  packet_.has_stop_waiting = false;
  packet_.has_crypto_handshake = NOT_HANDSHAKE;
  packet_.num_padding_bytes = 0;
  packet_.original_packet_number = 0;
  packet_.transmission_type = NOT_RETRANSMISSION;
  packet_.encrypted_buffer = nullptr;
  packet_.encrypted_length = 0;
  DCHECK(packet_.retransmittable_frames.empty());
  packet_.listeners.clear();
  packet_.largest_acked = 0;
  needs_full_padding_ = false;
}

void QuicPacketCreator::CreateAndSerializeStreamFrame(
    QuicStreamId id,
    const QuicIOVector& iov,
    QuicStreamOffset iov_offset,
    QuicStreamOffset stream_offset,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener,
    size_t* num_bytes_consumed) {
  DCHECK(queued_frames_.empty());
  // Write out the packet header
  QuicPacketHeader header;
  FillPacketHeader(&header);
  QUIC_CACHELINE_ALIGNED char encrypted_buffer[kMaxPacketSize];
  QuicDataWriter writer(arraysize(encrypted_buffer), encrypted_buffer,
                        framer_->endianness());
  if (!framer_->AppendPacketHeader(header, &writer)) {
    QUIC_BUG << "AppendPacketHeader failed";
    return;
  }

  // Create a Stream frame with the remaining space.
  QUIC_BUG_IF(iov_offset == iov.total_length && !fin)
      << "Creating a stream frame with no data or fin.";
  const size_t remaining_data_size = iov.total_length - iov_offset;
  const size_t min_frame_size = QuicFramer::GetMinStreamFrameSize(
      framer_->transport_version(), id, stream_offset,
      /* last_frame_in_packet= */ true);
  const size_t available_size =
      max_plaintext_size_ - writer.length() - min_frame_size;
  const size_t bytes_consumed =
      std::min<size_t>(available_size, remaining_data_size);

  const bool set_fin = fin && (bytes_consumed == remaining_data_size);
  std::unique_ptr<QuicStreamFrame> frame;
  if (framer_->HasDataProducer()) {
    frame = QuicMakeUnique<QuicStreamFrame>(id, set_fin, stream_offset,
                                            bytes_consumed);
  } else {
    UniqueStreamBuffer stream_buffer =
        NewStreamBuffer(buffer_allocator_, bytes_consumed);
    QuicUtils::CopyToBuffer(iov, iov_offset, bytes_consumed,
                            stream_buffer.get());
    frame = QuicMakeUnique<QuicStreamFrame>(
        id, set_fin, stream_offset, bytes_consumed, std::move(stream_buffer));
  }
  QUIC_DVLOG(1) << ENDPOINT << "Adding frame: " << *frame;

  // TODO(ianswett): AppendTypeByte and AppendStreamFrame could be optimized
  // into one method that takes a QuicStreamFrame, if warranted.
  if (!framer_->AppendTypeByte(QuicFrame(frame.get()),
                               /* no stream frame length */ true, &writer)) {
    QUIC_BUG << "AppendTypeByte failed";
    return;
  }
  if (!framer_->AppendStreamFrame(*frame, /* no stream frame length */ true,
                                  &writer)) {
    QUIC_BUG << "AppendStreamFrame failed";
    return;
  }

  size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header),
      writer.length(), arraysize(encrypted_buffer), encrypted_buffer);
  if (encrypted_length == 0) {
    QUIC_BUG << "Failed to encrypt packet number " << header.packet_number;
    return;
  }
  // TODO(ianswett): Optimize the storage so RetransmitableFrames can be
  // unioned with a QuicStreamFrame and a UniqueStreamBuffer.
  *num_bytes_consumed = bytes_consumed;
  packet_size_ = 0;
  packet_.encrypted_buffer = encrypted_buffer;
  packet_.encrypted_length = encrypted_length;
  if (ack_listener != nullptr) {
    packet_.listeners.emplace_back(std::move(ack_listener), bytes_consumed);
  }
  packet_.retransmittable_frames.push_back(QuicFrame(frame.release()));
  OnSerializedPacket();
}

bool QuicPacketCreator::HasPendingFrames() const {
  return !queued_frames_.empty();
}

bool QuicPacketCreator::HasPendingRetransmittableFrames() const {
  return !packet_.retransmittable_frames.empty();
}

size_t QuicPacketCreator::ExpansionOnNewFrame() const {
  // If the last frame in the packet is a stream frame, then it will expand to
  // include the stream_length field when a new frame is added.
  bool has_trailing_stream_frame =
      !queued_frames_.empty() && queued_frames_.back().type == STREAM_FRAME;
  return has_trailing_stream_frame ? kQuicStreamPayloadLengthSize : 0;
}

size_t QuicPacketCreator::BytesFree() {
  DCHECK_GE(max_plaintext_size_, PacketSize());
  return max_plaintext_size_ -
         std::min(max_plaintext_size_, PacketSize() + ExpansionOnNewFrame());
}

size_t QuicPacketCreator::PacketSize() {
  if (!queued_frames_.empty()) {
    return packet_size_;
  }
  packet_size_ =
      GetPacketHeaderSize(framer_->transport_version(), connection_id_length_,
                          send_version_in_packet_, IncludeNonceInPublicHeader(),
                          packet_.packet_number_length);
  return packet_size_;
}

bool QuicPacketCreator::AddSavedFrame(const QuicFrame& frame) {
  return AddFrame(frame, /*save_retransmittable_frames=*/true);
}

bool QuicPacketCreator::AddPaddedSavedFrame(const QuicFrame& frame) {
  if (AddFrame(frame, /*save_retransmittable_frames=*/true)) {
    needs_full_padding_ = true;
    return true;
  }
  return false;
}

void QuicPacketCreator::AddAckListener(
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener,
    QuicPacketLength length) {
  DCHECK(!queued_frames_.empty());
  packet_.listeners.emplace_back(std::move(ack_listener), length);
}

void QuicPacketCreator::SerializePacket(char* encrypted_buffer,
                                        size_t encrypted_buffer_len) {
  DCHECK_LT(0u, encrypted_buffer_len);
  QUIC_BUG_IF(queued_frames_.empty() && pending_padding_bytes_ == 0)
      << "Attempt to serialize empty packet";
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  MaybeAddPadding();

  DCHECK_GE(max_plaintext_size_, packet_size_);
  // Use the packet_size_ instead of the buffer size to ensure smaller
  // packet sizes are properly used.
  size_t length = framer_->BuildDataPacket(header, queued_frames_,
                                           encrypted_buffer, packet_size_);
  if (length == 0) {
    QUIC_BUG << "Failed to serialize " << queued_frames_.size() << " frames.";
    return;
  }

  // ACK Frames will be truncated due to length only if they're the only frame
  // in the packet, and if packet_size_ was set to max_plaintext_size_. If
  // truncation due to length occurred, then GetSerializedFrameLength will have
  // returned all bytes free.
  bool possibly_truncated_by_length = packet_size_ == max_plaintext_size_ &&
                                      queued_frames_.size() == 1 &&
                                      queued_frames_.back().type == ACK_FRAME;
  // Because of possible truncation, we can't be confident that our
  // packet size calculation worked correctly.
  if (!possibly_truncated_by_length) {
    DCHECK_EQ(packet_size_, length);
  }
  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      encrypted_buffer_len, encrypted_buffer);
  if (encrypted_length == 0) {
    QUIC_BUG << "Failed to encrypt packet number " << packet_.packet_number;
    return;
  }

  packet_size_ = 0;
  queued_frames_.clear();
  packet_.encrypted_buffer = encrypted_buffer;
  packet_.encrypted_length = encrypted_length;
}

std::unique_ptr<QuicEncryptedPacket>
QuicPacketCreator::SerializeVersionNegotiationPacket(
    const QuicTransportVersionVector& supported_versions) {
  DCHECK_EQ(Perspective::IS_SERVER, framer_->perspective());
  std::unique_ptr<QuicEncryptedPacket> encrypted =
      QuicFramer::BuildVersionNegotiationPacket(connection_id_,
                                                supported_versions);
  DCHECK(encrypted);
  DCHECK_GE(max_packet_length_, encrypted->length());
  return encrypted;
}

// TODO(jri): Make this a public method of framer?
SerializedPacket QuicPacketCreator::NoPacket() {
  return SerializedPacket(0, PACKET_1BYTE_PACKET_NUMBER, nullptr, 0, false,
                          false);
}

void QuicPacketCreator::FillPacketHeader(QuicPacketHeader* header) {
  header->public_header.connection_id = connection_id_;
  header->public_header.connection_id_length = connection_id_length_;
  header->public_header.reset_flag = false;
  header->public_header.version_flag = send_version_in_packet_;
  if (IncludeNonceInPublicHeader()) {
    DCHECK_EQ(Perspective::IS_SERVER, framer_->perspective());
    header->public_header.nonce = &diversification_nonce_;
  } else {
    header->public_header.nonce = nullptr;
  }
  header->packet_number = ++packet_.packet_number;
  header->public_header.packet_number_length = packet_.packet_number_length;
}

bool QuicPacketCreator::ShouldRetransmit(const QuicFrame& frame) {
  switch (frame.type) {
    case ACK_FRAME:
    case PADDING_FRAME:
    case STOP_WAITING_FRAME:
    case MTU_DISCOVERY_FRAME:
      return false;
    default:
      return true;
  }
}

bool QuicPacketCreator::AddFrame(const QuicFrame& frame,
                                 bool save_retransmittable_frames) {
  QUIC_DVLOG(1) << ENDPOINT << "Adding frame: " << frame;
  if (frame.type == STREAM_FRAME &&
      frame.stream_frame->stream_id != kCryptoStreamId &&
      packet_.encryption_level == ENCRYPTION_NONE) {
    const string error_details = "Cannot send stream data without encryption.";
    QUIC_BUG << error_details;
    delegate_->OnUnrecoverableError(
        QUIC_ATTEMPT_TO_SEND_UNENCRYPTED_STREAM_DATA, error_details,
        ConnectionCloseSource::FROM_SELF);
    return false;
  }
  size_t frame_len = framer_->GetSerializedFrameLength(
      frame, BytesFree(), queued_frames_.empty(), true,
      packet_.packet_number_length);
  if (frame_len == 0) {
    // Current open packet is full.
    Flush();
    return false;
  }
  DCHECK_LT(0u, packet_size_);
  packet_size_ += ExpansionOnNewFrame() + frame_len;

  if (save_retransmittable_frames && ShouldRetransmit(frame)) {
    if (packet_.retransmittable_frames.empty()) {
      packet_.retransmittable_frames.reserve(2);
    }
    packet_.retransmittable_frames.push_back(frame);
    queued_frames_.push_back(frame);
    if (frame.type == STREAM_FRAME &&
        frame.stream_frame->stream_id == kCryptoStreamId) {
      packet_.has_crypto_handshake = IS_HANDSHAKE;
    }
  } else {
    queued_frames_.push_back(frame);
  }

  if (frame.type == ACK_FRAME) {
    packet_.has_ack = true;
    packet_.largest_acked = frame.ack_frame->largest_observed;
  }
  if (frame.type == STOP_WAITING_FRAME) {
    packet_.has_stop_waiting = true;
  }
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnFrameAddedToPacket(frame);
  }

  return true;
}

void QuicPacketCreator::MaybeAddPadding() {
  // The current packet should have no padding bytes because padding is only
  // added when this method is called just before the packet is serialized.
  DCHECK_EQ(0, packet_.num_padding_bytes);
  if (BytesFree() == 0) {
    // Don't pad full packets.
    return;
  }

  if (!needs_full_padding_ && pending_padding_bytes_ == 0) {
    // Do not need padding.
    return;
  }

  if (needs_full_padding_) {
    // Full padding does not consume pending padding bytes.
    packet_.num_padding_bytes = -1;
  } else {
    packet_.num_padding_bytes =
        std::min<int16_t>(pending_padding_bytes_, BytesFree());
    pending_padding_bytes_ -= packet_.num_padding_bytes;
    QUIC_FLAG_COUNT(quic_reloadable_flag_quic_enable_random_padding);
  }

  bool success =
      AddFrame(QuicFrame(QuicPaddingFrame(packet_.num_padding_bytes)), false);
  DCHECK(success);
}

bool QuicPacketCreator::IncludeNonceInPublicHeader() {
  return have_diversification_nonce_ &&
         packet_.encryption_level == ENCRYPTION_INITIAL;
}

void QuicPacketCreator::AddPendingPadding(QuicByteCount size) {
  pending_padding_bytes_ += size;
}

bool QuicPacketCreator::StreamFrameStartsWithChlo(
    QuicIOVector iov,
    size_t iov_offset,
    const QuicStreamFrame& frame) const {
  if (!framer_->HasDataProducer()) {
    return frame.stream_id == kCryptoStreamId &&
           frame.data_length >= sizeof(kCHLO) &&
           strncmp(frame.data_buffer, reinterpret_cast<const char*>(&kCHLO),
                   sizeof(kCHLO)) == 0;
  }

  if (framer_->perspective() == Perspective::IS_SERVER ||
      frame.stream_id != kCryptoStreamId || frame.data_length < sizeof(kCHLO)) {
    return false;
  }
  return framer_->StartsWithChlo(frame.stream_id, frame.offset);
}

}  // namespace net
