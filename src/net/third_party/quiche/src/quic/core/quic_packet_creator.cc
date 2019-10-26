// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_aligned.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {
namespace {

QuicLongHeaderType EncryptionlevelToLongHeaderType(EncryptionLevel level) {
  switch (level) {
    case ENCRYPTION_INITIAL:
      return INITIAL;
    case ENCRYPTION_HANDSHAKE:
      return HANDSHAKE;
    case ENCRYPTION_ZERO_RTT:
      return ZERO_RTT_PROTECTED;
    case ENCRYPTION_FORWARD_SECURE:
      QUIC_BUG
          << "Try to derive long header type for packet with encryption level: "
          << QuicUtils::EncryptionLevelToString(level);
      return INVALID_PACKET_TYPE;
    default:
      QUIC_BUG << QuicUtils::EncryptionLevelToString(level);
      return INVALID_PACKET_TYPE;
  }
}

}  // namespace

#define ENDPOINT \
  (framer_->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicPacketCreator::QuicPacketCreator(QuicConnectionId server_connection_id,
                                     QuicFramer* framer,
                                     DelegateInterface* delegate)
    : QuicPacketCreator(server_connection_id,
                        framer,
                        QuicRandom::GetInstance(),
                        delegate) {}

QuicPacketCreator::QuicPacketCreator(QuicConnectionId server_connection_id,
                                     QuicFramer* framer,
                                     QuicRandom* random,
                                     DelegateInterface* delegate)
    : delegate_(delegate),
      debug_delegate_(nullptr),
      framer_(framer),
      random_(random),
      send_version_in_packet_(framer->perspective() == Perspective::IS_CLIENT),
      have_diversification_nonce_(false),
      max_packet_length_(0),
      server_connection_id_included_(CONNECTION_ID_PRESENT),
      packet_size_(0),
      server_connection_id_(server_connection_id),
      client_connection_id_(EmptyQuicConnectionId()),
      packet_(QuicPacketNumber(),
              PACKET_1BYTE_PACKET_NUMBER,
              nullptr,
              0,
              false,
              false),
      pending_padding_bytes_(0),
      needs_full_padding_(false),
      can_set_transmission_type_(false),
      fix_get_packet_header_size_(
          GetQuicReloadableFlag(quic_fix_get_packet_header_size)) {
  SetMaxPacketLength(kDefaultMaxPacketSize);
}

QuicPacketCreator::~QuicPacketCreator() {
  DeleteFrames(&packet_.retransmittable_frames);
}

void QuicPacketCreator::SetEncrypter(EncryptionLevel level,
                                     std::unique_ptr<QuicEncrypter> encrypter) {
  framer_->SetEncrypter(level, std::move(encrypter));
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
  QUIC_BUG_IF(max_plaintext_size_ - PacketHeaderSize() <
              MinPlaintextPacketSize(framer_->version()))
      << "Attempted to set max packet length too small";
}

// Stops serializing version of the protocol in packets sent after this call.
// A packet that is already open might send kQuicVersionSize bytes less than the
// maximum packet size if we stop sending version before it is serialized.
void QuicPacketCreator::StopSendingVersion() {
  DCHECK(send_version_in_packet_);
  DCHECK(!VersionHasIetfInvariantHeader(framer_->transport_version()));
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
  const uint64_t current_delta =
      packet_.packet_number + 1 - least_packet_awaited_by_peer;
  const uint64_t delta = std::max(current_delta, max_packets_in_flight);
  packet_.packet_number_length = QuicFramer::GetMinPacketNumberLength(
      framer_->transport_version(), QuicPacketNumber(delta * 4));
}

bool QuicPacketCreator::ConsumeCryptoData(EncryptionLevel level,
                                          size_t write_length,
                                          QuicStreamOffset offset,
                                          bool needs_full_padding,
                                          TransmissionType transmission_type,
                                          QuicFrame* frame) {
  if (!CreateCryptoFrame(level, write_length, offset, frame)) {
    return false;
  }
  // When crypto data was sent in stream frames, ConsumeData is called with
  // |needs_full_padding = true|. Keep the same behavior here when sending
  // crypto frames.
  //
  // TODO(nharper): Check what the IETF drafts say about padding out initial
  // messages and change this as appropriate.
  if (needs_full_padding) {
    needs_full_padding_ = true;
  }
  return AddFrame(*frame, /*save_retransmittable_frames*/ true,
                  transmission_type);
}

bool QuicPacketCreator::ConsumeData(QuicStreamId id,
                                    size_t data_size,
                                    QuicStreamOffset offset,
                                    bool fin,
                                    bool needs_full_padding,
                                    TransmissionType transmission_type,
                                    QuicFrame* frame) {
  if (!HasRoomForStreamFrame(id, offset, data_size)) {
    return false;
  }
  CreateStreamFrame(id, data_size, offset, fin, frame);
  // Explicitly disallow multi-packet CHLOs.
  if (GetQuicFlag(FLAGS_quic_enforce_single_packet_chlo) &&
      StreamFrameIsClientHello(frame->stream_frame) &&
      frame->stream_frame.data_length < data_size) {
    const std::string error_details =
        "Client hello won't fit in a single packet.";
    QUIC_BUG << error_details << " Constructed stream frame length: "
             << frame->stream_frame.data_length
             << " CHLO length: " << data_size;
    delegate_->OnUnrecoverableError(QUIC_CRYPTO_CHLO_TOO_LARGE, error_details);
    return false;
  }
  if (!AddFrame(*frame, /*save_retransmittable_frames=*/true,
                transmission_type)) {
    // Fails if we try to write unencrypted stream data.
    return false;
  }
  if (needs_full_padding) {
    needs_full_padding_ = true;
  }

  return true;
}

bool QuicPacketCreator::HasRoomForStreamFrame(QuicStreamId id,
                                              QuicStreamOffset offset,
                                              size_t data_size) {
  return BytesFree() >
         QuicFramer::GetMinStreamFrameSize(framer_->transport_version(), id,
                                           offset, true, data_size);
}

bool QuicPacketCreator::HasRoomForMessageFrame(QuicByteCount length) {
  return BytesFree() >= QuicFramer::GetMessageFrameSize(
                            framer_->transport_version(), true, length);
}

// TODO(fkastenholz): this method should not use constant values for
// the last-frame-in-packet and data-length parameters to
// GetMinStreamFrameSize.  Proper values should be plumbed in from
// higher up. This was left this way for now for a few reasons. First,
// higher up calls to StreamFramePacketOverhead() do not always know
// this information, leading to a cascade of changes and B) the
// higher-up software does not always loop, calling
// StreamFramePacketOverhead() once for every packet -- eg there is
// a test in quic_connection_test that calls it once and assumes that
// the value is the same for all packets.

// static
size_t QuicPacketCreator::StreamFramePacketOverhead(
    QuicTransportVersion version,
    QuicConnectionIdLength destination_connection_id_length,
    QuicConnectionIdLength source_connection_id_length,
    bool include_version,
    bool include_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    QuicVariableLengthIntegerLength retry_token_length_length,
    QuicVariableLengthIntegerLength length_length,
    QuicStreamOffset offset) {
  return GetPacketHeaderSize(version, destination_connection_id_length,
                             source_connection_id_length, include_version,
                             include_diversification_nonce,
                             packet_number_length, retry_token_length_length, 0,
                             length_length) +

         // Assumes this is a packet with a single stream frame in it. Since
         // last_frame_in_packet is set true, the size of the length field is
         // not included in the calculation. This is OK because in other places
         // in the code, the logic adds back 2 (the size of the Google QUIC
         // length) when a frame is not the last frame of the packet. This is
         // also acceptable for IETF Quic; even though the length field could be
         // 8 bytes long, in practice it will not be longer than 2 bytes (enough
         // to encode 16K).  A length that would be encoded in 2 bytes (0xfff)
         // is passed just for cleanliness.
         //
         // TODO(fkastenholz): This is very hacky and feels brittle. Ideally we
         // would calculate the correct lengths at the correct time, based on
         // the state at that time/place.
         QuicFramer::GetMinStreamFrameSize(version, 1u, offset, true,
                                           kMaxOutgoingPacketSize);
}

void QuicPacketCreator::CreateStreamFrame(QuicStreamId id,
                                          size_t data_size,
                                          QuicStreamOffset offset,
                                          bool fin,
                                          QuicFrame* frame) {
  DCHECK_GT(
      max_packet_length_,
      StreamFramePacketOverhead(
          framer_->transport_version(), GetDestinationConnectionIdLength(),
          GetSourceConnectionIdLength(), kIncludeVersion,
          IncludeNonceInPublicHeader(), PACKET_6BYTE_PACKET_NUMBER,
          GetRetryTokenLengthLength(), GetLengthLength(), offset));

  QUIC_BUG_IF(!HasRoomForStreamFrame(id, offset, data_size))
      << "No room for Stream frame, BytesFree: " << BytesFree()
      << " MinStreamFrameSize: "
      << QuicFramer::GetMinStreamFrameSize(framer_->transport_version(), id,
                                           offset, true, data_size);

  QUIC_BUG_IF(data_size == 0 && !fin)
      << "Creating a stream frame for stream ID:" << id
      << " with no data or fin.";
  size_t min_frame_size = QuicFramer::GetMinStreamFrameSize(
      framer_->transport_version(), id, offset,
      /* last_frame_in_packet= */ true, data_size);
  size_t bytes_consumed =
      std::min<size_t>(BytesFree() - min_frame_size, data_size);

  bool set_fin = fin && bytes_consumed == data_size;  // Last frame.
  *frame = QuicFrame(QuicStreamFrame(id, set_fin, offset, bytes_consumed));
}

bool QuicPacketCreator::CreateCryptoFrame(EncryptionLevel level,
                                          size_t write_length,
                                          QuicStreamOffset offset,
                                          QuicFrame* frame) {
  size_t min_frame_size =
      QuicFramer::GetMinCryptoFrameSize(write_length, offset);
  if (BytesFree() <= min_frame_size) {
    return false;
  }
  size_t max_write_length = BytesFree() - min_frame_size;
  size_t bytes_consumed = std::min<size_t>(max_write_length, write_length);
  *frame = QuicFrame(new QuicCryptoFrame(level, offset, bytes_consumed));
  return true;
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
    bool success = AddFrame(frame, false, retransmission.transmission_type);
    QUIC_BUG_IF(!success) << " Failed to add frame of type:" << frame.type
                          << " num_frames:"
                          << retransmission.retransmittable_frames.size()
                          << " retransmission.packet_number_length:"
                          << retransmission.packet_number_length
                          << " packet_.packet_number_length:"
                          << packet_.packet_number_length;
  }
  packet_.transmission_type = retransmission.transmission_type;
  SerializePacket(buffer, buffer_len);
  packet_.original_packet_number = retransmission.packet_number;
  OnSerializedPacket();
  // Restore old values.
  packet_.encryption_level = default_encryption_level;
}

void QuicPacketCreator::Flush() {
  if (!HasPendingFrames() && pending_padding_bytes_ == 0) {
    return;
  }

  QUIC_CACHELINE_ALIGNED char stack_buffer[kMaxOutgoingPacketSize];
  char* serialized_packet_buffer = delegate_->GetPacketBuffer();
  if (serialized_packet_buffer == nullptr) {
    serialized_packet_buffer = stack_buffer;
  }

  SerializePacket(serialized_packet_buffer, kMaxOutgoingPacketSize);
  OnSerializedPacket();
}

void QuicPacketCreator::OnSerializedPacket() {
  if (packet_.encrypted_buffer == nullptr) {
    const std::string error_details = "Failed to SerializePacket.";
    QUIC_BUG << error_details;
    delegate_->OnUnrecoverableError(QUIC_FAILED_TO_SERIALIZE_PACKET,
                                    error_details);
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
  packet_.original_packet_number.Clear();
  packet_.transmission_type = NOT_RETRANSMISSION;
  packet_.encrypted_buffer = nullptr;
  packet_.encrypted_length = 0;
  DCHECK(packet_.retransmittable_frames.empty());
  packet_.largest_acked.Clear();
  needs_full_padding_ = false;
}

void QuicPacketCreator::CreateAndSerializeStreamFrame(
    QuicStreamId id,
    size_t write_length,
    QuicStreamOffset iov_offset,
    QuicStreamOffset stream_offset,
    bool fin,
    TransmissionType transmission_type,
    size_t* num_bytes_consumed) {
  DCHECK(queued_frames_.empty());
  // Write out the packet header
  QuicPacketHeader header;
  FillPacketHeader(&header);

  QUIC_CACHELINE_ALIGNED char stack_buffer[kMaxOutgoingPacketSize];
  char* encrypted_buffer = delegate_->GetPacketBuffer();
  if (encrypted_buffer == nullptr) {
    encrypted_buffer = stack_buffer;
  }

  QuicDataWriter writer(kMaxOutgoingPacketSize, encrypted_buffer);
  size_t length_field_offset = 0;
  if (!framer_->AppendPacketHeader(header, &writer, &length_field_offset)) {
    QUIC_BUG << "AppendPacketHeader failed";
    return;
  }

  // Create a Stream frame with the remaining space.
  QUIC_BUG_IF(iov_offset == write_length && !fin)
      << "Creating a stream frame with no data or fin.";
  const size_t remaining_data_size = write_length - iov_offset;
  size_t min_frame_size = QuicFramer::GetMinStreamFrameSize(
      framer_->transport_version(), id, stream_offset,
      /* last_frame_in_packet= */ true, remaining_data_size);
  size_t available_size =
      max_plaintext_size_ - writer.length() - min_frame_size;
  size_t bytes_consumed = std::min<size_t>(available_size, remaining_data_size);
  size_t plaintext_bytes_written = min_frame_size + bytes_consumed;
  bool needs_padding = false;
  if (plaintext_bytes_written < MinPlaintextPacketSize(framer_->version())) {
    needs_padding = true;
    // Recalculate sizes with the stream frame not being marked as the last
    // frame in the packet.
    min_frame_size = QuicFramer::GetMinStreamFrameSize(
        framer_->transport_version(), id, stream_offset,
        /* last_frame_in_packet= */ false, remaining_data_size);
    available_size = max_plaintext_size_ - writer.length() - min_frame_size;
    bytes_consumed = std::min<size_t>(available_size, remaining_data_size);
    plaintext_bytes_written = min_frame_size + bytes_consumed;
  }

  const bool set_fin = fin && (bytes_consumed == remaining_data_size);
  QuicStreamFrame frame(id, set_fin, stream_offset, bytes_consumed);
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnFrameAddedToPacket(QuicFrame(frame));
  }
  QUIC_DVLOG(1) << ENDPOINT << "Adding frame: " << frame;

  QUIC_DVLOG(2) << ENDPOINT << "Serializing stream packet " << header << frame;

  // TODO(ianswett): AppendTypeByte and AppendStreamFrame could be optimized
  // into one method that takes a QuicStreamFrame, if warranted.
  bool omit_frame_length = !needs_padding;
  if (!framer_->AppendTypeByte(QuicFrame(frame), omit_frame_length, &writer)) {
    QUIC_BUG << "AppendTypeByte failed";
    return;
  }
  if (!framer_->AppendStreamFrame(frame, omit_frame_length, &writer)) {
    QUIC_BUG << "AppendStreamFrame failed";
    return;
  }
  if (needs_padding &&
      plaintext_bytes_written < MinPlaintextPacketSize(framer_->version()) &&
      !writer.WritePaddingBytes(MinPlaintextPacketSize(framer_->version()) -
                                plaintext_bytes_written)) {
    QUIC_BUG << "Unable to add padding bytes";
    return;
  }

  if (!framer_->WriteIetfLongHeaderLength(header, &writer, length_field_offset,
                                          packet_.encryption_level)) {
    return;
  }

  if (can_set_transmission_type()) {
    packet_.transmission_type = transmission_type;
  }

  size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header),
      writer.length(), kMaxOutgoingPacketSize, encrypted_buffer);
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
  packet_.retransmittable_frames.push_back(QuicFrame(frame));
  OnSerializedPacket();
}

bool QuicPacketCreator::HasPendingFrames() const {
  return !queued_frames_.empty();
}

bool QuicPacketCreator::HasPendingRetransmittableFrames() const {
  return !packet_.retransmittable_frames.empty();
}

bool QuicPacketCreator::HasPendingStreamFramesOfStream(QuicStreamId id) const {
  for (const auto& frame : packet_.retransmittable_frames) {
    if (frame.type == STREAM_FRAME && frame.stream_frame.stream_id == id) {
      return true;
    }
  }
  return false;
}

size_t QuicPacketCreator::ExpansionOnNewFrame() const {
  // If the last frame in the packet is a message frame, then it will expand to
  // include the varint message length when a new frame is added.
  const bool has_trailing_message_frame =
      !queued_frames_.empty() && queued_frames_.back().type == MESSAGE_FRAME;
  if (has_trailing_message_frame) {
    return QuicDataWriter::GetVarInt62Len(
        queued_frames_.back().message_frame->message_length);
  }
  // If the last frame in the packet is a stream frame, then it will expand to
  // include the stream_length field when a new frame is added.
  const bool has_trailing_stream_frame =
      !queued_frames_.empty() && queued_frames_.back().type == STREAM_FRAME;
  if (!has_trailing_stream_frame) {
    return 0;
  }
  if (VersionHasIetfQuicFrames(framer_->transport_version())) {
    return QuicDataWriter::GetVarInt62Len(
        queued_frames_.back().stream_frame.data_length);
  }
  return kQuicStreamPayloadLengthSize;
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
  packet_size_ = PacketHeaderSize();
  return packet_size_;
}

bool QuicPacketCreator::AddSavedFrame(const QuicFrame& frame,
                                      TransmissionType transmission_type) {
  return AddFrame(frame, /*save_retransmittable_frames=*/true,
                  transmission_type);
}

bool QuicPacketCreator::AddPaddedSavedFrame(
    const QuicFrame& frame,
    TransmissionType transmission_type) {
  if (AddFrame(frame, /*save_retransmittable_frames=*/true,
               transmission_type)) {
    needs_full_padding_ = true;
    return true;
  }
  return false;
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

  QUIC_DVLOG(2) << ENDPOINT << "Serializing packet " << header
                << QuicFramesToString(queued_frames_);

  DCHECK_GE(max_plaintext_size_, packet_size_);
  // Use the packet_size_ instead of the buffer size to ensure smaller
  // packet sizes are properly used.
  size_t length =
      framer_->BuildDataPacket(header, queued_frames_, encrypted_buffer,
                               packet_size_, packet_.encryption_level);
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
    bool ietf_quic,
    bool use_length_prefix,
    const ParsedQuicVersionVector& supported_versions) {
  DCHECK_EQ(Perspective::IS_SERVER, framer_->perspective());
  std::unique_ptr<QuicEncryptedPacket> encrypted =
      QuicFramer::BuildVersionNegotiationPacket(
          server_connection_id_, client_connection_id_, ietf_quic,
          use_length_prefix, supported_versions);
  DCHECK(encrypted);
  DCHECK_GE(max_packet_length_, encrypted->length());
  return encrypted;
}

OwningSerializedPacketPointer
QuicPacketCreator::SerializeConnectivityProbingPacket() {
  QUIC_BUG_IF(VersionHasIetfQuicFrames(framer_->transport_version()))
      << "Must not be version 99 to serialize padded ping connectivity probe";
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  QUIC_DVLOG(2) << ENDPOINT << "Serializing connectivity probing packet "
                << header;

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  size_t length = framer_->BuildConnectivityProbingPacket(
      header, buffer.get(), max_plaintext_size_, packet_.encryption_level);
  DCHECK(length);

  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      kMaxOutgoingPacketSize, buffer.get());
  DCHECK(encrypted_length);

  OwningSerializedPacketPointer serialize_packet(new SerializedPacket(
      header.packet_number, header.packet_number_length, buffer.release(),
      encrypted_length, /*has_ack=*/false, /*has_stop_waiting=*/false));

  serialize_packet->encryption_level = packet_.encryption_level;
  serialize_packet->transmission_type = NOT_RETRANSMISSION;

  return serialize_packet;
}

OwningSerializedPacketPointer
QuicPacketCreator::SerializePathChallengeConnectivityProbingPacket(
    QuicPathFrameBuffer* payload) {
  QUIC_BUG_IF(!VersionHasIetfQuicFrames(framer_->transport_version()))
      << "Must be version 99 to serialize path challenge connectivity probe, "
         "is version "
      << framer_->transport_version();
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  QUIC_DVLOG(2) << ENDPOINT << "Serializing path challenge packet " << header;

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  size_t length = framer_->BuildPaddedPathChallengePacket(
      header, buffer.get(), max_plaintext_size_, payload, random_,
      packet_.encryption_level);
  DCHECK(length);

  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      kMaxOutgoingPacketSize, buffer.get());
  DCHECK(encrypted_length);

  OwningSerializedPacketPointer serialize_packet(new SerializedPacket(
      header.packet_number, header.packet_number_length, buffer.release(),
      encrypted_length, /*has_ack=*/false, /*has_stop_waiting=*/false));

  serialize_packet->encryption_level = packet_.encryption_level;
  serialize_packet->transmission_type = NOT_RETRANSMISSION;

  return serialize_packet;
}

OwningSerializedPacketPointer
QuicPacketCreator::SerializePathResponseConnectivityProbingPacket(
    const QuicDeque<QuicPathFrameBuffer>& payloads,
    const bool is_padded) {
  QUIC_BUG_IF(!VersionHasIetfQuicFrames(framer_->transport_version()))
      << "Must be version 99 to serialize path response connectivity probe, is "
         "version "
      << framer_->transport_version();
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  QUIC_DVLOG(2) << ENDPOINT << "Serializing path response packet " << header;

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  size_t length = framer_->BuildPathResponsePacket(
      header, buffer.get(), max_plaintext_size_, payloads, is_padded,
      packet_.encryption_level);
  DCHECK(length);

  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      kMaxOutgoingPacketSize, buffer.get());
  DCHECK(encrypted_length);

  OwningSerializedPacketPointer serialize_packet(new SerializedPacket(
      header.packet_number, header.packet_number_length, buffer.release(),
      encrypted_length, /*has_ack=*/false, /*has_stop_waiting=*/false));

  serialize_packet->encryption_level = packet_.encryption_level;
  serialize_packet->transmission_type = NOT_RETRANSMISSION;

  return serialize_packet;
}

// TODO(b/74062209): Make this a public method of framer?
SerializedPacket QuicPacketCreator::NoPacket() {
  return SerializedPacket(QuicPacketNumber(), PACKET_1BYTE_PACKET_NUMBER,
                          nullptr, 0, false, false);
}

QuicConnectionId QuicPacketCreator::GetDestinationConnectionId() const {
  if (framer_->perspective() == Perspective::IS_SERVER) {
    return client_connection_id_;
  }
  return server_connection_id_;
}

QuicConnectionId QuicPacketCreator::GetSourceConnectionId() const {
  if (framer_->perspective() == Perspective::IS_CLIENT) {
    return client_connection_id_;
  }
  return server_connection_id_;
}

QuicConnectionIdIncluded QuicPacketCreator::GetDestinationConnectionIdIncluded()
    const {
  // In versions that do not support client connection IDs, the destination
  // connection ID is only sent from client to server.
  return (framer_->perspective() == Perspective::IS_CLIENT ||
          framer_->version().SupportsClientConnectionIds())
             ? CONNECTION_ID_PRESENT
             : CONNECTION_ID_ABSENT;
}

QuicConnectionIdIncluded QuicPacketCreator::GetSourceConnectionIdIncluded()
    const {
  // Long header packets sent by server include source connection ID.
  // Ones sent by the client only include source connection ID if the version
  // supports client connection IDs.
  if (HasIetfLongHeader() &&
      (framer_->perspective() == Perspective::IS_SERVER ||
       framer_->version().SupportsClientConnectionIds())) {
    return CONNECTION_ID_PRESENT;
  }
  if (framer_->perspective() == Perspective::IS_SERVER) {
    return server_connection_id_included_;
  }
  return CONNECTION_ID_ABSENT;
}

QuicConnectionIdLength QuicPacketCreator::GetDestinationConnectionIdLength()
    const {
  DCHECK(QuicUtils::IsConnectionIdValidForVersion(server_connection_id_,
                                                  transport_version()));
  return GetDestinationConnectionIdIncluded() == CONNECTION_ID_PRESENT
             ? static_cast<QuicConnectionIdLength>(
                   GetDestinationConnectionId().length())
             : PACKET_0BYTE_CONNECTION_ID;
}

QuicConnectionIdLength QuicPacketCreator::GetSourceConnectionIdLength() const {
  DCHECK(QuicUtils::IsConnectionIdValidForVersion(server_connection_id_,
                                                  transport_version()));
  return GetSourceConnectionIdIncluded() == CONNECTION_ID_PRESENT
             ? static_cast<QuicConnectionIdLength>(
                   GetSourceConnectionId().length())
             : PACKET_0BYTE_CONNECTION_ID;
}

QuicPacketNumberLength QuicPacketCreator::GetPacketNumberLength() const {
  if (HasIetfLongHeader() &&
      !framer_->version().SendsVariableLengthPacketNumberInLongHeader()) {
    return PACKET_4BYTE_PACKET_NUMBER;
  }
  return packet_.packet_number_length;
}

size_t QuicPacketCreator::PacketHeaderSize() const {
  return GetPacketHeaderSize(
      framer_->transport_version(), GetDestinationConnectionIdLength(),
      GetSourceConnectionIdLength(), IncludeVersionInHeader(),
      IncludeNonceInPublicHeader(), GetPacketNumberLength(),
      GetRetryTokenLengthLength(), GetRetryToken().length(), GetLengthLength());
}

QuicVariableLengthIntegerLength QuicPacketCreator::GetRetryTokenLengthLength()
    const {
  if (QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      HasIetfLongHeader() &&
      EncryptionlevelToLongHeaderType(packet_.encryption_level) == INITIAL) {
    return QuicDataWriter::GetVarInt62Len(GetRetryToken().length());
  }
  return VARIABLE_LENGTH_INTEGER_LENGTH_0;
}

QuicStringPiece QuicPacketCreator::GetRetryToken() const {
  if (QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      HasIetfLongHeader() &&
      EncryptionlevelToLongHeaderType(packet_.encryption_level) == INITIAL) {
    return retry_token_;
  }
  return QuicStringPiece();
}

void QuicPacketCreator::SetRetryToken(QuicStringPiece retry_token) {
  retry_token_ = std::string(retry_token);
}

QuicVariableLengthIntegerLength QuicPacketCreator::GetLengthLength() const {
  if (QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      HasIetfLongHeader()) {
    QuicLongHeaderType long_header_type =
        EncryptionlevelToLongHeaderType(packet_.encryption_level);
    if (long_header_type == INITIAL || long_header_type == ZERO_RTT_PROTECTED ||
        long_header_type == HANDSHAKE) {
      return VARIABLE_LENGTH_INTEGER_LENGTH_2;
    }
  }
  return VARIABLE_LENGTH_INTEGER_LENGTH_0;
}

void QuicPacketCreator::FillPacketHeader(QuicPacketHeader* header) {
  header->destination_connection_id = GetDestinationConnectionId();
  header->destination_connection_id_included =
      GetDestinationConnectionIdIncluded();
  header->source_connection_id = GetSourceConnectionId();
  header->source_connection_id_included = GetSourceConnectionIdIncluded();
  header->reset_flag = false;
  header->version_flag = IncludeVersionInHeader();
  if (IncludeNonceInPublicHeader()) {
    DCHECK_EQ(Perspective::IS_SERVER, framer_->perspective());
    header->nonce = &diversification_nonce_;
  } else {
    header->nonce = nullptr;
  }
  packet_.packet_number = NextSendingPacketNumber();
  header->packet_number = packet_.packet_number;
  header->packet_number_length = GetPacketNumberLength();
  header->retry_token_length_length = GetRetryTokenLengthLength();
  header->retry_token = GetRetryToken();
  header->length_length = GetLengthLength();
  header->remaining_packet_length = 0;
  if (!HasIetfLongHeader()) {
    return;
  }
  header->long_packet_type =
      EncryptionlevelToLongHeaderType(packet_.encryption_level);
}

bool QuicPacketCreator::AddFrame(const QuicFrame& frame,
                                 bool save_retransmittable_frames,
                                 TransmissionType transmission_type) {
  QUIC_DVLOG(1) << ENDPOINT << "Adding frame with transmission type "
                << transmission_type << ": " << frame;
  if (frame.type == STREAM_FRAME &&
      !QuicUtils::IsCryptoStreamId(framer_->transport_version(),
                                   frame.stream_frame.stream_id) &&
      (packet_.encryption_level == ENCRYPTION_INITIAL ||
       packet_.encryption_level == ENCRYPTION_HANDSHAKE)) {
    const std::string error_details = QuicStrCat(
        "Cannot send stream data with level: ",
        QuicUtils::EncryptionLevelToString(packet_.encryption_level));
    QUIC_BUG << error_details;
    delegate_->OnUnrecoverableError(
        QUIC_ATTEMPT_TO_SEND_UNENCRYPTED_STREAM_DATA, error_details);
    return false;
  }
  size_t frame_len = framer_->GetSerializedFrameLength(
      frame, BytesFree(), queued_frames_.empty(),
      /* last_frame_in_packet= */ true, GetPacketNumberLength());
  if (frame_len == 0) {
    // Current open packet is full.
    Flush();
    return false;
  }
  DCHECK_LT(0u, packet_size_);

  packet_size_ += ExpansionOnNewFrame() + frame_len;

  if (save_retransmittable_frames &&
      QuicUtils::IsRetransmittableFrame(frame.type)) {
    packet_.retransmittable_frames.push_back(frame);
    queued_frames_.push_back(frame);
    if (QuicUtils::IsHandshakeFrame(frame, framer_->transport_version())) {
      packet_.has_crypto_handshake = IS_HANDSHAKE;
    }
  } else {
    queued_frames_.push_back(frame);
  }

  if (frame.type == ACK_FRAME) {
    packet_.has_ack = true;
    packet_.largest_acked = LargestAcked(*frame.ack_frame);
  }
  if (frame.type == STOP_WAITING_FRAME) {
    packet_.has_stop_waiting = true;
  }
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnFrameAddedToPacket(frame);
  }

  // Packet transmission type is determined by the last added retransmittable
  // frame.
  if (can_set_transmission_type() &&
      QuicUtils::IsRetransmittableFrame(frame.type)) {
    packet_.transmission_type = transmission_type;
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

  if (packet_.transmission_type == PROBING_RETRANSMISSION) {
    needs_full_padding_ = true;
  }

  // Header protection requires a minimum plaintext packet size.
  size_t extra_padding_bytes = 0;
  if (framer_->version().HasHeaderProtection()) {
    size_t frame_bytes = PacketSize() - PacketHeaderSize();

    if (frame_bytes + pending_padding_bytes_ <
            MinPlaintextPacketSize(framer_->version()) &&
        !needs_full_padding_) {
      extra_padding_bytes =
          MinPlaintextPacketSize(framer_->version()) - frame_bytes;
    }
  }

  if (!needs_full_padding_ && pending_padding_bytes_ == 0 &&
      extra_padding_bytes == 0) {
    // Do not need padding.
    return;
  }

  int padding_bytes = -1;
  if (needs_full_padding_) {
    // Full padding does not consume pending padding bytes.
    packet_.num_padding_bytes = -1;
  } else {
    packet_.num_padding_bytes =
        std::min<int16_t>(pending_padding_bytes_, BytesFree());
    pending_padding_bytes_ -= packet_.num_padding_bytes;
    padding_bytes =
        std::max<int16_t>(packet_.num_padding_bytes, extra_padding_bytes);
  }

  bool success = AddFrame(QuicFrame(QuicPaddingFrame(padding_bytes)), false,
                          packet_.transmission_type);
  DCHECK(success);
}

bool QuicPacketCreator::IncludeNonceInPublicHeader() const {
  return have_diversification_nonce_ &&
         packet_.encryption_level == ENCRYPTION_ZERO_RTT;
}

bool QuicPacketCreator::IncludeVersionInHeader() const {
  if (VersionHasIetfInvariantHeader(framer_->transport_version())) {
    return packet_.encryption_level < ENCRYPTION_FORWARD_SECURE;
  }
  return send_version_in_packet_;
}

void QuicPacketCreator::AddPendingPadding(QuicByteCount size) {
  pending_padding_bytes_ += size;
}

bool QuicPacketCreator::StreamFrameIsClientHello(
    const QuicStreamFrame& frame) const {
  if (framer_->perspective() == Perspective::IS_SERVER ||
      !QuicUtils::IsCryptoStreamId(framer_->transport_version(),
                                   frame.stream_id)) {
    return false;
  }
  // The ClientHello is always sent with INITIAL encryption.
  return packet_.encryption_level == ENCRYPTION_INITIAL;
}

void QuicPacketCreator::SetServerConnectionIdIncluded(
    QuicConnectionIdIncluded server_connection_id_included) {
  DCHECK(server_connection_id_included == CONNECTION_ID_PRESENT ||
         server_connection_id_included == CONNECTION_ID_ABSENT);
  DCHECK(framer_->perspective() == Perspective::IS_SERVER ||
         server_connection_id_included != CONNECTION_ID_ABSENT);
  server_connection_id_included_ = server_connection_id_included;
}

void QuicPacketCreator::SetServerConnectionId(
    QuicConnectionId server_connection_id) {
  server_connection_id_ = server_connection_id;
}

void QuicPacketCreator::SetClientConnectionId(
    QuicConnectionId client_connection_id) {
  DCHECK(client_connection_id.IsEmpty() ||
         framer_->version().SupportsClientConnectionIds());
  client_connection_id_ = client_connection_id;
}

void QuicPacketCreator::SetTransmissionType(TransmissionType type) {
  DCHECK(can_set_transmission_type_);

  if (!can_set_transmission_type()) {
    QUIC_DVLOG_IF(1, type != packet_.transmission_type)
        << ENDPOINT << "Setting Transmission type to "
        << QuicUtils::TransmissionTypeToString(type);

    packet_.transmission_type = type;
  }
}

QuicPacketLength QuicPacketCreator::GetCurrentLargestMessagePayload() const {
  if (!VersionSupportsMessageFrames(framer_->transport_version())) {
    return 0;
  }
  const size_t packet_header_size = GetPacketHeaderSize(
      framer_->transport_version(), GetDestinationConnectionIdLength(),
      GetSourceConnectionIdLength(), IncludeVersionInHeader(),
      IncludeNonceInPublicHeader(), GetPacketNumberLength(),
      // No Retry token on packets containing application data.
      VARIABLE_LENGTH_INTEGER_LENGTH_0, 0, GetLengthLength());
  // This is the largest possible message payload when the length field is
  // omitted.
  return max_plaintext_size_ -
         std::min(max_plaintext_size_, packet_header_size + kQuicFrameTypeSize);
}

QuicPacketLength QuicPacketCreator::GetGuaranteedLargestMessagePayload() const {
  if (!VersionSupportsMessageFrames(framer_->transport_version())) {
    return 0;
  }
  // QUIC Crypto server packets may include a diversification nonce.
  const bool may_include_nonce =
      framer_->version().handshake_protocol == PROTOCOL_QUIC_CRYPTO &&
      framer_->perspective() == Perspective::IS_SERVER;
  // IETF QUIC long headers include a length on client 0RTT packets.
  QuicVariableLengthIntegerLength length_length =
      VARIABLE_LENGTH_INTEGER_LENGTH_0;
  if (framer_->perspective() == Perspective::IS_CLIENT) {
    length_length = VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }
  if (!QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      fix_get_packet_header_size_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_fix_get_packet_header_size, 3, 3);
    length_length = VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  const size_t packet_header_size = GetPacketHeaderSize(
      framer_->transport_version(), GetDestinationConnectionIdLength(),
      // Assume CID lengths don't change, but version may be present.
      GetSourceConnectionIdLength(), kIncludeVersion, may_include_nonce,
      PACKET_4BYTE_PACKET_NUMBER,
      // No Retry token on packets containing application data.
      VARIABLE_LENGTH_INTEGER_LENGTH_0, 0, length_length);
  // This is the largest possible message payload when the length field is
  // omitted.
  const QuicPacketLength largest_payload =
      max_plaintext_size_ -
      std::min(max_plaintext_size_, packet_header_size + kQuicFrameTypeSize);
  // This must always be less than or equal to GetCurrentLargestMessagePayload.
  DCHECK_LE(largest_payload, GetCurrentLargestMessagePayload());
  return largest_payload;
}

bool QuicPacketCreator::HasIetfLongHeader() const {
  return VersionHasIetfInvariantHeader(framer_->transport_version()) &&
         packet_.encryption_level < ENCRYPTION_FORWARD_SECURE;
}

// static
size_t QuicPacketCreator::MinPlaintextPacketSize(
    const ParsedQuicVersion& version) {
  if (!version.HasHeaderProtection()) {
    return 0;
  }
  // Header protection samples 16 bytes of ciphertext starting 4 bytes after the
  // packet number. In IETF QUIC, all AEAD algorithms have a 16-byte auth tag
  // (i.e. the ciphertext is 16 bytes larger than the plaintext). Since packet
  // numbers could be as small as 1 byte, but the sample starts 4 bytes after
  // the packet number, at least 3 bytes of plaintext are needed to make sure
  // that there is enough ciphertext to sample.
  //
  // Google QUIC crypto uses different AEAD algorithms - in particular the auth
  // tags are only 12 bytes instead of 16 bytes. Since the auth tag is 4 bytes
  // shorter, 4 more bytes of plaintext are needed to guarantee there is enough
  // ciphertext to sample.
  //
  // This method could check for PROTOCOL_TLS1_3 vs PROTOCOL_QUIC_CRYPTO and
  // return 3 when TLS 1.3 is in use (the use of IETF vs Google QUIC crypters is
  // determined based on the handshake protocol used). However, even when TLS
  // 1.3 is used, unittests still use NullEncrypter/NullDecrypter (and other
  // test crypters) which also only use 12 byte tags.
  //
  // TODO(nharper): Set this based on the handshake protocol in use.
  return 7;
}

QuicPacketNumber QuicPacketCreator::NextSendingPacketNumber() const {
  if (!packet_number().IsInitialized()) {
    return framer_->first_sending_packet_number();
  }
  return packet_number() + 1;
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
