// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packet_creator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_path_challenge_frame.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_aligned.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_exported_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_server_stats.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

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
          << EncryptionLevelToString(level);
      return INVALID_PACKET_TYPE;
    default:
      QUIC_BUG << EncryptionLevelToString(level);
      return INVALID_PACKET_TYPE;
  }
}

void LogCoalesceStreamFrameStatus(bool success) {
  QUIC_HISTOGRAM_BOOL("QuicSession.CoalesceStreamFrameStatus", success,
                      "Success rate of coalesing stream frames attempt.");
}

// ScopedPacketContextSwitcher saves |packet|'s states and change states
// during its construction. When the switcher goes out of scope, it restores
// saved states.
class ScopedPacketContextSwitcher {
 public:
  ScopedPacketContextSwitcher(QuicPacketNumber packet_number,
                              QuicPacketNumberLength packet_number_length,
                              EncryptionLevel encryption_level,
                              SerializedPacket* packet)

      : saved_packet_number_(packet->packet_number),
        saved_packet_number_length_(packet->packet_number_length),
        saved_encryption_level_(packet->encryption_level),
        packet_(packet) {
    packet_->packet_number = packet_number,
    packet_->packet_number_length = packet_number_length;
    packet_->encryption_level = encryption_level;
  }

  ~ScopedPacketContextSwitcher() {
    packet_->packet_number = saved_packet_number_;
    packet_->packet_number_length = saved_packet_number_length_;
    packet_->encryption_level = saved_encryption_level_;
  }

 private:
  const QuicPacketNumber saved_packet_number_;
  const QuicPacketNumberLength saved_packet_number_length_;
  const EncryptionLevel saved_encryption_level_;
  SerializedPacket* packet_;
};

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
      next_transmission_type_(NOT_RETRANSMISSION),
      flusher_attached_(false),
      fully_pad_crypto_handshake_packets_(true),
      latched_hard_max_packet_length_(0) {
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

void QuicPacketCreator::SetSoftMaxPacketLength(QuicByteCount length) {
  DCHECK(CanSetMaxPacketLength());
  if (length > max_packet_length_) {
    QUIC_BUG << ENDPOINT
             << "Try to increase max_packet_length_ in "
                "SetSoftMaxPacketLength, use SetMaxPacketLength instead.";
    return;
  }
  if (framer_->GetMaxPlaintextSize(length) <
      PacketHeaderSize() + MinPlaintextPacketSize(framer_->version())) {
    QUIC_DLOG(INFO) << length << " is too small to fit packet header";
    return;
  }
  QUIC_DVLOG(1) << "Setting soft max packet length to: " << length;
  latched_hard_max_packet_length_ = max_packet_length_;
  max_packet_length_ = length;
  max_plaintext_size_ = framer_->GetMaxPlaintextSize(length);
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
  packet_.packet_number_length =
      QuicFramer::GetMinPacketNumberLength(QuicPacketNumber(delta * 4));
}

void QuicPacketCreator::SkipNPacketNumbers(
    QuicPacketCount count,
    QuicPacketNumber least_packet_awaited_by_peer,
    QuicPacketCount max_packets_in_flight) {
  if (!queued_frames_.empty()) {
    // Don't change creator state if there are frames queued.
    QUIC_BUG << "Called SkipNPacketNumbers with " << queued_frames_.size()
             << " queued_frames.  First frame type:"
             << queued_frames_.front().type
             << " last frame type:" << queued_frames_.back().type;
    return;
  }
  if (packet_.packet_number > packet_.packet_number + count) {
    // Skipping count packet numbers causes packet number wrapping around,
    // reject it.
    QUIC_LOG(WARNING) << "Skipping " << count
                      << " packet numbers causes packet number wrapping "
                         "around, least_packet_awaited_by_peer: "
                      << least_packet_awaited_by_peer
                      << " packet_number:" << packet_.packet_number;
    return;
  }
  packet_.packet_number += count;
  // Packet number changes, update packet number length if necessary.
  UpdatePacketNumberLength(least_packet_awaited_by_peer, max_packets_in_flight);
}

bool QuicPacketCreator::ConsumeCryptoDataToFillCurrentPacket(
    EncryptionLevel level,
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
  return AddFrame(*frame, transmission_type);
}

bool QuicPacketCreator::ConsumeDataToFillCurrentPacket(
    QuicStreamId id,
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
  if (!AddFrame(*frame, transmission_type)) {
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
  const size_t min_stream_frame_size = QuicFramer::GetMinStreamFrameSize(
      framer_->transport_version(), id, offset, /*last_frame_in_packet=*/true,
      data_size);
  if (BytesFree() > min_stream_frame_size) {
    return true;
  }
  if (!RemoveSoftMaxPacketLength()) {
    return false;
  }
  return BytesFree() > min_stream_frame_size;
}

bool QuicPacketCreator::HasRoomForMessageFrame(QuicByteCount length) {
  const size_t message_frame_size = QuicFramer::GetMessageFrameSize(
      framer_->transport_version(), /*last_frame_in_packet=*/true, length);
  if (BytesFree() >= message_frame_size) {
    return true;
  }
  if (!RemoveSoftMaxPacketLength()) {
    return false;
  }
  return BytesFree() >= message_frame_size;
}

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

         // Assumes a packet with a single stream frame, which omits the length,
         // causing the data length argument to be ignored.
         QuicFramer::GetMinStreamFrameSize(version, 1u, offset, true,
                                           kMaxOutgoingPacketSize /* unused */);
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
  if (BytesFree() <= min_frame_size &&
      (!RemoveSoftMaxPacketLength() || BytesFree() <= min_frame_size)) {
    return false;
  }
  size_t max_write_length = BytesFree() - min_frame_size;
  size_t bytes_consumed = std::min<size_t>(max_write_length, write_length);
  *frame = QuicFrame(new QuicCryptoFrame(level, offset, bytes_consumed));
  return true;
}

void QuicPacketCreator::FlushCurrentPacket() {
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
  RemoveSoftMaxPacketLength();
  delegate_->OnSerializedPacket(&packet);
}

void QuicPacketCreator::ClearPacket() {
  packet_.has_ack = false;
  packet_.has_stop_waiting = false;
  packet_.has_crypto_handshake = NOT_HANDSHAKE;
  packet_.num_padding_bytes = 0;
  packet_.transmission_type = NOT_RETRANSMISSION;
  packet_.encrypted_buffer = nullptr;
  packet_.encrypted_length = 0;
  DCHECK(packet_.retransmittable_frames.empty());
  DCHECK(packet_.nonretransmittable_frames.empty());
  packet_.largest_acked.Clear();
  needs_full_padding_ = false;
}

size_t QuicPacketCreator::ReserializeInitialPacketInCoalescedPacket(
    const SerializedPacket& packet,
    size_t padding_size,
    char* buffer,
    size_t buffer_len) {
  QUIC_BUG_IF(packet.encryption_level != ENCRYPTION_INITIAL);
  QUIC_BUG_IF(packet.nonretransmittable_frames.empty() &&
              packet.retransmittable_frames.empty())
      << "Attempt to serialize empty ENCRYPTION_INITIAL packet in coalesced "
         "packet";
  ScopedPacketContextSwitcher switcher(
      packet.packet_number -
          1,  // -1 because serialize packet increase packet number.
      packet.packet_number_length, packet.encryption_level, &packet_);
  for (const QuicFrame& frame : packet.nonretransmittable_frames) {
    if (!AddFrame(frame, packet.transmission_type)) {
      QUIC_BUG << "Failed to serialize frame: " << frame;
      return 0;
    }
  }
  for (const QuicFrame& frame : packet.retransmittable_frames) {
    if (!AddFrame(frame, packet.transmission_type)) {
      QUIC_BUG << "Failed to serialize frame: " << frame;
      return 0;
    }
  }
  // Add necessary padding.
  if (padding_size > 0) {
    QUIC_DVLOG(2) << ENDPOINT << "Add padding of size: " << padding_size;
    if (!AddFrame(QuicFrame(QuicPaddingFrame(padding_size)),
                  packet.transmission_type)) {
      QUIC_BUG << "Failed to add padding of size " << padding_size
               << " when serializing ENCRYPTION_INITIAL "
                  "packet in coalesced packet";
      return 0;
    }
  }
  SerializePacket(buffer, buffer_len);
  const size_t encrypted_length = packet_.encrypted_length;
  // Clear frames in packet_. No need to DeleteFrames since frames are owned by
  // initial_packet.
  packet_.retransmittable_frames.clear();
  packet_.nonretransmittable_frames.clear();
  ClearPacket();
  return encrypted_length;
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

  packet_.transmission_type = transmission_type;

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

bool QuicPacketCreator::AddPaddedSavedFrame(
    const QuicFrame& frame,
    TransmissionType transmission_type) {
  if (AddFrame(frame, transmission_type)) {
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
                << QuicFramesToString(queued_frames_) << " at encryption_level "
                << EncryptionLevelToString(packet_.encryption_level);

  if (!framer_->HasEncrypterOfEncryptionLevel(packet_.encryption_level)) {
    QUIC_BUG << ENDPOINT << "Attempting to serialize " << header
             << QuicFramesToString(queued_frames_)
             << " at missing encryption_level "
             << EncryptionLevelToString(packet_.encryption_level) << " using "
             << framer_->version();
    return;
  }

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
  RemoveSoftMaxPacketLength();
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  QUIC_DVLOG(2) << ENDPOINT << "Serializing connectivity probing packet "
                << header;

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  size_t length = BuildConnectivityProbingPacket(
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
  RemoveSoftMaxPacketLength();
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  QUIC_DVLOG(2) << ENDPOINT << "Serializing path challenge packet " << header;

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  size_t length = BuildPaddedPathChallengePacket(
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
    const QuicCircularDeque<QuicPathFrameBuffer>& payloads,
    const bool is_padded) {
  QUIC_BUG_IF(!VersionHasIetfQuicFrames(framer_->transport_version()))
      << "Must be version 99 to serialize path response connectivity probe, is "
         "version "
      << framer_->transport_version();
  RemoveSoftMaxPacketLength();
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  QUIC_DVLOG(2) << ENDPOINT << "Serializing path response packet " << header;

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  size_t length =
      BuildPathResponsePacket(header, buffer.get(), max_plaintext_size_,
                              payloads, is_padded, packet_.encryption_level);
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

size_t QuicPacketCreator::BuildPaddedPathChallengePacket(
    const QuicPacketHeader& header,
    char* buffer,
    size_t packet_length,
    QuicPathFrameBuffer* payload,
    QuicRandom* randomizer,
    EncryptionLevel level) {
  DCHECK(VersionHasIetfQuicFrames(framer_->transport_version()));
  QuicFrames frames;

  // Write a PATH_CHALLENGE frame, which has a random 8-byte payload
  randomizer->RandBytes(payload->data(), payload->size());
  QuicPathChallengeFrame path_challenge_frame(0, *payload);
  frames.push_back(QuicFrame(&path_challenge_frame));

  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnFrameAddedToPacket(QuicFrame(&path_challenge_frame));
  }

  // Add padding to the rest of the packet in order to assess Path MTU
  // characteristics.
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(padding_frame));

  return framer_->BuildDataPacket(header, frames, buffer, packet_length, level);
}

size_t QuicPacketCreator::BuildPathResponsePacket(
    const QuicPacketHeader& header,
    char* buffer,
    size_t packet_length,
    const QuicCircularDeque<QuicPathFrameBuffer>& payloads,
    const bool is_padded,
    EncryptionLevel level) {
  if (payloads.empty()) {
    QUIC_BUG
        << "Attempt to generate connectivity response with no request payloads";
    return 0;
  }
  DCHECK(VersionHasIetfQuicFrames(framer_->transport_version()));

  std::vector<std::unique_ptr<QuicPathResponseFrame>> path_response_frames;
  for (const QuicPathFrameBuffer& payload : payloads) {
    // Note that the control frame ID can be 0 since this is not retransmitted.
    path_response_frames.push_back(
        std::make_unique<QuicPathResponseFrame>(0, payload));
  }

  QuicFrames frames;
  for (const std::unique_ptr<QuicPathResponseFrame>& path_response_frame :
       path_response_frames) {
    frames.push_back(QuicFrame(path_response_frame.get()));
    if (debug_delegate_ != nullptr) {
      debug_delegate_->OnFrameAddedToPacket(
          QuicFrame(path_response_frame.get()));
    }
  }

  if (is_padded) {
    // Add padding to the rest of the packet in order to assess Path MTU
    // characteristics.
    QuicPaddingFrame padding_frame;
    frames.push_back(QuicFrame(padding_frame));
  }

  return framer_->BuildDataPacket(header, frames, buffer, packet_length, level);
}

size_t QuicPacketCreator::BuildConnectivityProbingPacket(
    const QuicPacketHeader& header,
    char* buffer,
    size_t packet_length,
    EncryptionLevel level) {
  QuicFrames frames;

  // Write a PING frame, which has no data payload.
  QuicPingFrame ping_frame;
  frames.push_back(QuicFrame(ping_frame));

  // Add padding to the rest of the packet.
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(padding_frame));

  return framer_->BuildDataPacket(header, frames, buffer, packet_length, level);
}

size_t QuicPacketCreator::SerializeCoalescedPacket(
    const QuicCoalescedPacket& coalesced,
    char* buffer,
    size_t buffer_len) {
  QUIC_BUG_IF(packet_.num_padding_bytes != 0);
  if (HasPendingFrames()) {
    QUIC_BUG << "Try to serialize coalesced packet with pending frames";
    return 0;
  }
  RemoveSoftMaxPacketLength();
  QUIC_BUG_IF(coalesced.length() == 0)
      << "Attempt to serialize empty coalesced packet";
  size_t packet_length = 0;
  if (coalesced.initial_packet() != nullptr) {
    // Padding coalesced packet containing initial packet to full.
    size_t padding_size = coalesced.max_packet_length() - coalesced.length();
    if (framer_->perspective() == Perspective::IS_SERVER &&
        QuicUtils::ContainsFrameType(
            coalesced.initial_packet()->retransmittable_frames,
            CONNECTION_CLOSE_FRAME)) {
      // Do not pad server initial connection close packet.
      padding_size = 0;
    }
    size_t initial_length = ReserializeInitialPacketInCoalescedPacket(
        *coalesced.initial_packet(), padding_size, buffer, buffer_len);
    if (initial_length == 0) {
      QUIC_BUG << "Failed to reserialize ENCRYPTION_INITIAL packet in "
                  "coalesced packet";
      return 0;
    }
    buffer += initial_length;
    buffer_len -= initial_length;
    packet_length += initial_length;
  }
  size_t length_copied = 0;
  if (!coalesced.CopyEncryptedBuffers(buffer, buffer_len, &length_copied)) {
    return 0;
  }
  packet_length += length_copied;
  QUIC_DVLOG(1) << ENDPOINT
                << "Successfully serialized coalesced packet of length: "
                << packet_length;
  return packet_length;
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

quiche::QuicheStringPiece QuicPacketCreator::GetRetryToken() const {
  if (QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      HasIetfLongHeader() &&
      EncryptionlevelToLongHeaderType(packet_.encryption_level) == INITIAL) {
    return retry_token_;
  }
  return quiche::QuicheStringPiece();
}

void QuicPacketCreator::SetRetryToken(quiche::QuicheStringPiece retry_token) {
  retry_token_ = std::string(retry_token);
}

bool QuicPacketCreator::ConsumeRetransmittableControlFrame(
    const QuicFrame& frame) {
  QUIC_BUG_IF(IsControlFrame(frame.type) && !GetControlFrameId(frame))
      << "Adding a control frame with no control frame id: " << frame;
  DCHECK(QuicUtils::IsRetransmittableFrame(frame.type)) << frame;
  MaybeBundleAckOpportunistically();
  if (HasPendingFrames()) {
    if (AddFrame(frame, next_transmission_type_)) {
      // There is pending frames and current frame fits.
      return true;
    }
  }
  DCHECK(!HasPendingFrames());
  if (frame.type != PING_FRAME && frame.type != CONNECTION_CLOSE_FRAME &&
      !delegate_->ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA,
                                       NOT_HANDSHAKE)) {
    // Do not check congestion window for ping or connection close frames.
    return false;
  }
  const bool success = AddFrame(frame, next_transmission_type_);
  QUIC_BUG_IF(!success) << "Failed to add frame:" << frame
                        << " transmission_type:" << next_transmission_type_;
  return success;
}

QuicConsumedData QuicPacketCreator::ConsumeData(QuicStreamId id,
                                                size_t write_length,
                                                QuicStreamOffset offset,
                                                StreamSendingState state) {
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to write stream data.";
  bool has_handshake = QuicUtils::IsCryptoStreamId(transport_version(), id);
  MaybeBundleAckOpportunistically();
  bool fin = state != NO_FIN;
  QUIC_BUG_IF(has_handshake && fin)
      << "Handshake packets should never send a fin";
  // To make reasoning about crypto frames easier, we don't combine them with
  // other retransmittable frames in a single packet.
  if (has_handshake && HasPendingRetransmittableFrames()) {
    FlushCurrentPacket();
  }

  size_t total_bytes_consumed = 0;
  bool fin_consumed = false;

  if (!HasRoomForStreamFrame(id, offset, write_length)) {
    FlushCurrentPacket();
  }

  if (!fin && (write_length == 0)) {
    QUIC_BUG << "Attempt to consume empty data without FIN.";
    return QuicConsumedData(0, false);
  }
  // We determine if we can enter the fast path before executing
  // the slow path loop.
  bool run_fast_path =
      !has_handshake && state != FIN_AND_PADDING && !HasPendingFrames() &&
      write_length - total_bytes_consumed > kMaxOutgoingPacketSize &&
      latched_hard_max_packet_length_ == 0;

  while (!run_fast_path && delegate_->ShouldGeneratePacket(
                               HAS_RETRANSMITTABLE_DATA,
                               has_handshake ? IS_HANDSHAKE : NOT_HANDSHAKE)) {
    QuicFrame frame;
    bool needs_full_padding =
        has_handshake && fully_pad_crypto_handshake_packets_;

    if (!ConsumeDataToFillCurrentPacket(id, write_length - total_bytes_consumed,
                                        offset + total_bytes_consumed, fin,
                                        needs_full_padding,
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
           (bytes_consumed > 0 && HasPendingFrames()));

    if (total_bytes_consumed == write_length) {
      // We're done writing the data. Exit the loop.
      // We don't make this a precondition because we could have 0 bytes of data
      // if we're simply writing a fin.
      break;
    }
    FlushCurrentPacket();

    run_fast_path =
        !has_handshake && state != FIN_AND_PADDING && !HasPendingFrames() &&
        write_length - total_bytes_consumed > kMaxOutgoingPacketSize &&
        latched_hard_max_packet_length_ == 0;
  }

  if (run_fast_path) {
    return ConsumeDataFastPath(id, write_length, offset, state != NO_FIN,
                               total_bytes_consumed);
  }

  // Don't allow the handshake to be bundled with other retransmittable frames.
  if (has_handshake) {
    FlushCurrentPacket();
  }

  return QuicConsumedData(total_bytes_consumed, fin_consumed);
}

QuicConsumedData QuicPacketCreator::ConsumeDataFastPath(
    QuicStreamId id,
    size_t write_length,
    QuicStreamOffset offset,
    bool fin,
    size_t total_bytes_consumed) {
  DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id));

  while (total_bytes_consumed < write_length &&
         delegate_->ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA,
                                         NOT_HANDSHAKE)) {
    // Serialize and encrypt the packet.
    size_t bytes_consumed = 0;
    CreateAndSerializeStreamFrame(id, write_length, total_bytes_consumed,
                                  offset + total_bytes_consumed, fin,
                                  next_transmission_type_, &bytes_consumed);
    if (bytes_consumed == 0) {
      const std::string error_details =
          "Failed in CreateAndSerializeStreamFrame.";
      QUIC_BUG << error_details;
      delegate_->OnUnrecoverableError(QUIC_FAILED_TO_SERIALIZE_PACKET,
                                      error_details);
      break;
    }
    total_bytes_consumed += bytes_consumed;
  }

  return QuicConsumedData(total_bytes_consumed,
                          fin && (total_bytes_consumed == write_length));
}

size_t QuicPacketCreator::ConsumeCryptoData(EncryptionLevel level,
                                            size_t write_length,
                                            QuicStreamOffset offset) {
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to write crypto data.";
  MaybeBundleAckOpportunistically();
  // To make reasoning about crypto frames easier, we don't combine them with
  // other retransmittable frames in a single packet.
  // TODO(nharper): Once we have separate packet number spaces, everything
  // should be driven by encryption level, and we should stop flushing in this
  // spot.
  if (HasPendingRetransmittableFrames()) {
    FlushCurrentPacket();
  }

  size_t total_bytes_consumed = 0;

  while (total_bytes_consumed < write_length) {
    QuicFrame frame;
    if (!ConsumeCryptoDataToFillCurrentPacket(
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
    FlushCurrentPacket();
  }

  // Don't allow the handshake to be bundled with other retransmittable frames.
  FlushCurrentPacket();

  return total_bytes_consumed;
}

void QuicPacketCreator::GenerateMtuDiscoveryPacket(QuicByteCount target_mtu) {
  // MTU discovery frames must be sent by themselves.
  if (!CanSetMaxPacketLength()) {
    QUIC_BUG << "MTU discovery packets should only be sent when no other "
             << "frames needs to be sent.";
    return;
  }
  const QuicByteCount current_mtu = max_packet_length();

  // The MTU discovery frame is allocated on the stack, since it is going to be
  // serialized within this function.
  QuicMtuDiscoveryFrame mtu_discovery_frame;
  QuicFrame frame(mtu_discovery_frame);

  // Send the probe packet with the new length.
  SetMaxPacketLength(target_mtu);
  const bool success = AddPaddedSavedFrame(frame, next_transmission_type_);
  FlushCurrentPacket();
  // The only reason AddFrame can fail is that the packet is too full to fit in
  // a ping.  This is not possible for any sane MTU.
  QUIC_BUG_IF(!success) << "Failed to send path MTU target_mtu:" << target_mtu
                        << " transmission_type:" << next_transmission_type_;

  // Reset the packet length back.
  SetMaxPacketLength(current_mtu);
}

void QuicPacketCreator::MaybeBundleAckOpportunistically() {
  if (has_ack()) {
    // Ack already queued, nothing to do.
    return;
  }
  if (!delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                       NOT_HANDSHAKE)) {
    return;
  }
  const bool flushed =
      FlushAckFrame(delegate_->MaybeBundleAckOpportunistically());
  QUIC_BUG_IF(!flushed) << "Failed to flush ACK frame. encryption_level:"
                        << packet_.encryption_level;
}

bool QuicPacketCreator::FlushAckFrame(const QuicFrames& frames) {
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to send ACK frame.";
  for (const auto& frame : frames) {
    DCHECK(frame.type == ACK_FRAME || frame.type == STOP_WAITING_FRAME);
    if (HasPendingFrames()) {
      if (AddFrame(frame, next_transmission_type_)) {
        // There is pending frames and current frame fits.
        continue;
      }
    }
    DCHECK(!HasPendingFrames());
    // There is no pending frames, consult the delegate whether a packet can be
    // generated.
    if (!delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                         NOT_HANDSHAKE)) {
      return false;
    }
    const bool success = AddFrame(frame, next_transmission_type_);
    QUIC_BUG_IF(!success) << "Failed to flush " << frame;
  }
  return true;
}

void QuicPacketCreator::AddRandomPadding() {
  AddPendingPadding(random_->RandUint64() % kMaxNumRandomPaddingBytes + 1);
}

void QuicPacketCreator::AttachPacketFlusher() {
  flusher_attached_ = true;
  if (!write_start_packet_number_.IsInitialized()) {
    write_start_packet_number_ = NextSendingPacketNumber();
  }
}

void QuicPacketCreator::Flush() {
  FlushCurrentPacket();
  SendRemainingPendingPadding();
  flusher_attached_ = false;
  if (GetQuicFlag(FLAGS_quic_export_server_num_packets_per_write_histogram)) {
    if (!write_start_packet_number_.IsInitialized()) {
      QUIC_BUG << "write_start_packet_number is not initialized";
      return;
    }
    QUIC_SERVER_HISTOGRAM_COUNTS(
        "quic_server_num_written_packets_per_write",
        NextSendingPacketNumber() - write_start_packet_number_, 1, 200, 50,
        "Number of QUIC packets written per write operation");
  }
  write_start_packet_number_.Clear();
}

void QuicPacketCreator::SendRemainingPendingPadding() {
  while (
      pending_padding_bytes() > 0 && !HasPendingFrames() &&
      delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA, NOT_HANDSHAKE)) {
    FlushCurrentPacket();
  }
}

void QuicPacketCreator::SetServerConnectionIdLength(uint32_t length) {
  if (length == 0) {
    SetServerConnectionIdIncluded(CONNECTION_ID_ABSENT);
  } else {
    SetServerConnectionIdIncluded(CONNECTION_ID_PRESENT);
  }
}

void QuicPacketCreator::SetTransmissionType(TransmissionType type) {
  next_transmission_type_ = type;
}

MessageStatus QuicPacketCreator::AddMessageFrame(QuicMessageId message_id,
                                                 QuicMemSliceSpan message) {
  QUIC_BUG_IF(!flusher_attached_) << "Packet flusher is not attached when "
                                     "generator tries to add message frame.";
  MaybeBundleAckOpportunistically();
  const QuicByteCount message_length = message.total_length();
  if (message_length > GetCurrentLargestMessagePayload()) {
    return MESSAGE_STATUS_TOO_LARGE;
  }
  if (!HasRoomForMessageFrame(message_length)) {
    FlushCurrentPacket();
  }
  QuicMessageFrame* frame = new QuicMessageFrame(message_id, message);
  const bool success = AddFrame(QuicFrame(frame), next_transmission_type_);
  if (!success) {
    QUIC_BUG << "Failed to send message " << message_id;
    delete frame;
    return MESSAGE_STATUS_INTERNAL_ERROR;
  }
  return MESSAGE_STATUS_SUCCESS;
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
                                 TransmissionType transmission_type) {
  QUIC_DVLOG(1) << ENDPOINT << "Adding frame with transmission type "
                << TransmissionTypeToString(transmission_type) << ": " << frame;
  if (frame.type == STREAM_FRAME &&
      !QuicUtils::IsCryptoStreamId(framer_->transport_version(),
                                   frame.stream_frame.stream_id) &&
      (packet_.encryption_level == ENCRYPTION_INITIAL ||
       packet_.encryption_level == ENCRYPTION_HANDSHAKE)) {
    const std::string error_details =
        quiche::QuicheStrCat("Cannot send stream data with level: ",
                             EncryptionLevelToString(packet_.encryption_level));
    QUIC_BUG << error_details;
    delegate_->OnUnrecoverableError(
        QUIC_ATTEMPT_TO_SEND_UNENCRYPTED_STREAM_DATA, error_details);
    return false;
  }

  if (frame.type == STREAM_FRAME) {
    if (MaybeCoalesceStreamFrame(frame.stream_frame)) {
      LogCoalesceStreamFrameStatus(true);
      return true;
    } else {
      LogCoalesceStreamFrameStatus(false);
    }
  }

  size_t frame_len = framer_->GetSerializedFrameLength(
      frame, BytesFree(), queued_frames_.empty(),
      /* last_frame_in_packet= */ true, GetPacketNumberLength());
  if (frame_len == 0 && RemoveSoftMaxPacketLength()) {
    // Remove soft max_packet_length and retry.
    frame_len = framer_->GetSerializedFrameLength(
        frame, BytesFree(), queued_frames_.empty(),
        /* last_frame_in_packet= */ true, GetPacketNumberLength());
  }
  if (frame_len == 0) {
    // Current open packet is full.
    FlushCurrentPacket();
    return false;
  }
  DCHECK_LT(0u, packet_size_);

  packet_size_ += ExpansionOnNewFrame() + frame_len;

  if (QuicUtils::IsRetransmittableFrame(frame.type)) {
    packet_.retransmittable_frames.push_back(frame);
    queued_frames_.push_back(frame);
    if (QuicUtils::IsHandshakeFrame(frame, framer_->transport_version())) {
      packet_.has_crypto_handshake = IS_HANDSHAKE;
    }
  } else {
    if (frame.type == PADDING_FRAME &&
        frame.padding_frame.num_padding_bytes == -1) {
      // Populate the actual length of full padding frame, such that one can
      // know how much padding is actually added.
      packet_.nonretransmittable_frames.push_back(
          QuicFrame(QuicPaddingFrame(frame_len)));
    } else {
      packet_.nonretransmittable_frames.push_back(frame);
    }
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
  if (QuicUtils::IsRetransmittableFrame(frame.type)) {
    packet_.transmission_type = transmission_type;
  }
  return true;
}

bool QuicPacketCreator::MaybeCoalesceStreamFrame(const QuicStreamFrame& frame) {
  if (queued_frames_.empty() || queued_frames_.back().type != STREAM_FRAME) {
    return false;
  }
  QuicStreamFrame* candidate = &queued_frames_.back().stream_frame;
  if (candidate->stream_id != frame.stream_id ||
      candidate->offset + candidate->data_length != frame.offset ||
      frame.data_length > BytesFree()) {
    return false;
  }
  candidate->data_length += frame.data_length;
  candidate->fin = frame.fin;

  // The back of retransmittable frames must be the same as the original
  // queued frames' back.
  DCHECK_EQ(packet_.retransmittable_frames.back().type, STREAM_FRAME);
  QuicStreamFrame* retransmittable =
      &packet_.retransmittable_frames.back().stream_frame;
  DCHECK_EQ(retransmittable->stream_id, frame.stream_id);
  DCHECK_EQ(retransmittable->offset + retransmittable->data_length,
            frame.offset);
  retransmittable->data_length = candidate->data_length;
  retransmittable->fin = candidate->fin;
  packet_size_ += frame.data_length;
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnStreamFrameCoalesced(*candidate);
  }
  return true;
}

bool QuicPacketCreator::RemoveSoftMaxPacketLength() {
  if (latched_hard_max_packet_length_ == 0) {
    return false;
  }
  if (!CanSetMaxPacketLength()) {
    return false;
  }
  QUIC_DVLOG(1) << "Restoring max packet length to: "
                << latched_hard_max_packet_length_;
  SetMaxPacketLength(latched_hard_max_packet_length_);
  // Reset latched_max_packet_length_.
  latched_hard_max_packet_length_ = 0;
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

  // Packet coalescer pads INITIAL packets, so the creator should not.
  if (framer_->version().CanSendCoalescedPackets() &&
      (packet_.encryption_level == ENCRYPTION_INITIAL ||
       packet_.encryption_level == ENCRYPTION_HANDSHAKE)) {
    // TODO(fayang): MTU discovery packets should not ever be sent as
    // ENCRYPTION_INITIAL or ENCRYPTION_HANDSHAKE.
    bool is_mtu_discovery = false;
    for (const auto& frame : packet_.nonretransmittable_frames) {
      if (frame.type == MTU_DISCOVERY_FRAME) {
        is_mtu_discovery = true;
        break;
      }
    }
    if (!is_mtu_discovery) {
      // Do not add full padding if connection tries to coalesce packet.
      needs_full_padding_ = false;
    }
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

  bool success = AddFrame(QuicFrame(QuicPaddingFrame(padding_bytes)),
                          packet_.transmission_type);
  QUIC_BUG_IF(!success) << "Failed to add padding_bytes: " << padding_bytes
                        << " transmission_type: "
                        << TransmissionTypeToString(packet_.transmission_type);
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
  size_t max_plaintext_size =
      latched_hard_max_packet_length_ == 0
          ? max_plaintext_size_
          : framer_->GetMaxPlaintextSize(latched_hard_max_packet_length_);
  return max_plaintext_size -
         std::min(max_plaintext_size, packet_header_size + kQuicFrameTypeSize);
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
  if (!QuicVersionHasLongHeaderLengths(framer_->transport_version())) {
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
  size_t max_plaintext_size =
      latched_hard_max_packet_length_ == 0
          ? max_plaintext_size_
          : framer_->GetMaxPlaintextSize(latched_hard_max_packet_length_);
  const QuicPacketLength largest_payload =
      max_plaintext_size -
      std::min(max_plaintext_size, packet_header_size + kQuicFrameTypeSize);
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

bool QuicPacketCreator::PacketFlusherAttached() const {
  return flusher_attached_;
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
