// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_packet_creator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/frames/quic_padding_frame.h"
#include "quiche/quic/core/frames/quic_path_challenge_frame.h"
#include "quiche/quic/core/frames/quic_stream_frame.h"
#include "quiche/quic/core/quic_chaos_protector.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_exported_stats.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/platform/api/quic_server_stats.h"
#include "quiche/common/print_elements.h"

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
      QUIC_BUG(quic_bug_12398_1)
          << "Try to derive long header type for packet with encryption level: "
          << level;
      return INVALID_PACKET_TYPE;
    default:
      QUIC_BUG(quic_bug_10752_1) << level;
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
    : QuicPacketCreator(server_connection_id, framer, QuicRandom::GetInstance(),
                        delegate) {}

QuicPacketCreator::QuicPacketCreator(QuicConnectionId server_connection_id,
                                     QuicFramer* framer, QuicRandom* random,
                                     DelegateInterface* delegate)
    : delegate_(delegate),
      debug_delegate_(nullptr),
      framer_(framer),
      random_(random),
      have_diversification_nonce_(false),
      max_packet_length_(0),
      server_connection_id_included_(CONNECTION_ID_PRESENT),
      packet_size_(0),
      server_connection_id_(server_connection_id),
      client_connection_id_(EmptyQuicConnectionId()),
      packet_(QuicPacketNumber(), PACKET_1BYTE_PACKET_NUMBER, nullptr, 0, false,
              false),
      pending_padding_bytes_(0),
      needs_full_padding_(false),
      next_transmission_type_(NOT_RETRANSMISSION),
      flusher_attached_(false),
      fully_pad_crypto_handshake_packets_(true),
      latched_hard_max_packet_length_(0),
      max_datagram_frame_size_(0) {
  SetMaxPacketLength(kDefaultMaxPacketSize);
  if (!framer_->version().UsesTls()) {
    // QUIC+TLS negotiates the maximum datagram frame size via the
    // IETF QUIC max_datagram_frame_size transport parameter.
    // QUIC_CRYPTO however does not negotiate this so we set its value here.
    SetMaxDatagramFrameSize(kMaxAcceptedDatagramFrameSize);
  }
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
  QUICHE_DCHECK(CanSetMaxPacketLength()) << ENDPOINT;

  // Avoid recomputing |max_plaintext_size_| if the length does not actually
  // change.
  if (length == max_packet_length_) {
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "Updating packet creator max packet length from "
                << max_packet_length_ << " to " << length;

  max_packet_length_ = length;
  max_plaintext_size_ = framer_->GetMaxPlaintextSize(max_packet_length_);
  QUIC_BUG_IF(
      quic_bug_12398_2,
      max_plaintext_size_ - PacketHeaderSize() <
          MinPlaintextPacketSize(framer_->version(), GetPacketNumberLength()))
      << ENDPOINT << "Attempted to set max packet length too small";
}

void QuicPacketCreator::SetMaxDatagramFrameSize(
    QuicByteCount max_datagram_frame_size) {
  constexpr QuicByteCount upper_bound =
      std::min<QuicByteCount>(std::numeric_limits<QuicPacketLength>::max(),
                              std::numeric_limits<size_t>::max());
  if (max_datagram_frame_size > upper_bound) {
    // A value of |max_datagram_frame_size| that is equal or greater than
    // 2^16-1 is effectively infinite because QUIC packets cannot be that large.
    // We therefore clamp the value here to allow us to safely cast
    // |max_datagram_frame_size_| to QuicPacketLength or size_t.
    max_datagram_frame_size = upper_bound;
  }
  max_datagram_frame_size_ = max_datagram_frame_size;
}

void QuicPacketCreator::SetSoftMaxPacketLength(QuicByteCount length) {
  QUICHE_DCHECK(CanSetMaxPacketLength()) << ENDPOINT;
  if (length > max_packet_length_) {
    QUIC_BUG(quic_bug_10752_2)
        << ENDPOINT
        << "Try to increase max_packet_length_ in "
           "SetSoftMaxPacketLength, use SetMaxPacketLength instead.";
    return;
  }
  if (framer_->GetMaxPlaintextSize(length) <
      PacketHeaderSize() +
          MinPlaintextPacketSize(framer_->version(), GetPacketNumberLength())) {
    // Please note: this would not guarantee to fit next packet if the size of
    // packet header increases (e.g., encryption level changes).
    QUIC_DLOG(INFO) << ENDPOINT << length
                    << " is too small to fit packet header";
    RemoveSoftMaxPacketLength();
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "Setting soft max packet length to: " << length;
  latched_hard_max_packet_length_ = max_packet_length_;
  max_packet_length_ = length;
  max_plaintext_size_ = framer_->GetMaxPlaintextSize(length);
}

void QuicPacketCreator::SetDiversificationNonce(
    const DiversificationNonce& nonce) {
  QUICHE_DCHECK(!have_diversification_nonce_) << ENDPOINT;
  have_diversification_nonce_ = true;
  diversification_nonce_ = nonce;
}

void QuicPacketCreator::UpdatePacketNumberLength(
    QuicPacketNumber least_packet_awaited_by_peer,
    QuicPacketCount max_packets_in_flight) {
  if (!queued_frames_.empty()) {
    // Don't change creator state if there are frames queued.
    QUIC_BUG(quic_bug_10752_3)
        << ENDPOINT << "Called UpdatePacketNumberLength with "
        << queued_frames_.size()
        << " queued_frames.  First frame type:" << queued_frames_.front().type
        << " last frame type:" << queued_frames_.back().type;
    return;
  }

  const QuicPacketNumber next_packet_number = NextSendingPacketNumber();
  QUICHE_DCHECK_LE(least_packet_awaited_by_peer, next_packet_number)
      << ENDPOINT;
  const uint64_t current_delta =
      next_packet_number - least_packet_awaited_by_peer;
  const uint64_t delta = std::max(current_delta, max_packets_in_flight);
  const QuicPacketNumberLength packet_number_length =
      QuicFramer::GetMinPacketNumberLength(QuicPacketNumber(delta * 4));
  if (packet_.packet_number_length == packet_number_length) {
    return;
  }
  QUIC_DVLOG(1) << ENDPOINT << "Updating packet number length from "
                << static_cast<int>(packet_.packet_number_length) << " to "
                << static_cast<int>(packet_number_length)
                << ", least_packet_awaited_by_peer: "
                << least_packet_awaited_by_peer
                << " max_packets_in_flight: " << max_packets_in_flight
                << " next_packet_number: " << next_packet_number;
  packet_.packet_number_length = packet_number_length;
}

void QuicPacketCreator::SkipNPacketNumbers(
    QuicPacketCount count, QuicPacketNumber least_packet_awaited_by_peer,
    QuicPacketCount max_packets_in_flight) {
  if (!queued_frames_.empty()) {
    // Don't change creator state if there are frames queued.
    QUIC_BUG(quic_bug_10752_4)
        << ENDPOINT << "Called SkipNPacketNumbers with "
        << queued_frames_.size()
        << " queued_frames.  First frame type:" << queued_frames_.front().type
        << " last frame type:" << queued_frames_.back().type;
    return;
  }
  if (packet_.packet_number > packet_.packet_number + count) {
    // Skipping count packet numbers causes packet number wrapping around,
    // reject it.
    QUIC_LOG(WARNING) << ENDPOINT << "Skipping " << count
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
    EncryptionLevel level, size_t write_length, QuicStreamOffset offset,
    bool needs_full_padding, TransmissionType transmission_type,
    QuicFrame* frame) {
  QUIC_DVLOG(2) << ENDPOINT << "ConsumeCryptoDataToFillCurrentPacket " << level
                << " write_length " << write_length << " offset " << offset
                << (needs_full_padding ? " needs_full_padding" : "") << " "
                << transmission_type;
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
    QuicStreamId id, size_t data_size, QuicStreamOffset offset, bool fin,
    bool needs_full_padding, TransmissionType transmission_type,
    QuicFrame* frame) {
  if (!HasRoomForStreamFrame(id, offset, data_size)) {
    return false;
  }
  CreateStreamFrame(id, data_size, offset, fin, frame);
  // Explicitly disallow multi-packet CHLOs.
  if (GetQuicFlag(quic_enforce_single_packet_chlo) &&
      StreamFrameIsClientHello(frame->stream_frame) &&
      frame->stream_frame.data_length < data_size) {
    const std::string error_details =
        "Client hello won't fit in a single packet.";
    QUIC_BUG(quic_bug_10752_5)
        << ENDPOINT << error_details << " Constructed stream frame length: "
        << frame->stream_frame.data_length << " CHLO length: " << data_size;
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
  const size_t message_frame_size =
      QuicFramer::GetMessageFrameSize(/*last_frame_in_packet=*/true, length);
  if (static_cast<QuicByteCount>(message_frame_size) >
      max_datagram_frame_size_) {
    return false;
  }
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
    QuicTransportVersion version, uint8_t destination_connection_id_length,
    uint8_t source_connection_id_length, bool include_version,
    bool include_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    quiche::QuicheVariableLengthIntegerLength retry_token_length_length,
    quiche::QuicheVariableLengthIntegerLength length_length,
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

void QuicPacketCreator::CreateStreamFrame(QuicStreamId id, size_t data_size,
                                          QuicStreamOffset offset, bool fin,
                                          QuicFrame* frame) {
  // Make sure max_packet_length_ is greater than the largest possible overhead
  // or max_packet_length_ is set to the soft limit.
  QUICHE_DCHECK(
      max_packet_length_ >
          StreamFramePacketOverhead(
              framer_->transport_version(), GetDestinationConnectionIdLength(),
              GetSourceConnectionIdLength(), kIncludeVersion,
              IncludeNonceInPublicHeader(), PACKET_6BYTE_PACKET_NUMBER,
              GetRetryTokenLengthLength(), GetLengthLength(), offset) ||
      latched_hard_max_packet_length_ > 0)
      << ENDPOINT;

  QUIC_BUG_IF(quic_bug_12398_3, !HasRoomForStreamFrame(id, offset, data_size))
      << ENDPOINT << "No room for Stream frame, BytesFree: " << BytesFree()
      << " MinStreamFrameSize: "
      << QuicFramer::GetMinStreamFrameSize(framer_->transport_version(), id,
                                           offset, true, data_size);

  QUIC_BUG_IF(quic_bug_12398_4, data_size == 0 && !fin)
      << ENDPOINT << "Creating a stream frame for stream ID:" << id
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
  const size_t min_frame_size =
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

  ABSL_CACHELINE_ALIGNED char stack_buffer[kMaxOutgoingPacketSize];
  QuicOwnedPacketBuffer external_buffer(delegate_->GetPacketBuffer());

  if (external_buffer.buffer == nullptr) {
    external_buffer.buffer = stack_buffer;
    external_buffer.release_buffer = nullptr;
  }

  QUICHE_DCHECK_EQ(nullptr, packet_.encrypted_buffer) << ENDPOINT;
  if (!SerializePacket(std::move(external_buffer), kMaxOutgoingPacketSize,
                       /*allow_padding=*/true)) {
    return;
  }
  OnSerializedPacket();
}

void QuicPacketCreator::OnSerializedPacket() {
  QUIC_BUG_IF(quic_bug_12398_5, packet_.encrypted_buffer == nullptr)
      << ENDPOINT;

  // Clear bytes_not_retransmitted for packets containing only
  // NOT_RETRANSMISSION frames.
  if (packet_.transmission_type == NOT_RETRANSMISSION) {
    packet_.bytes_not_retransmitted.reset();
  }

  SerializedPacket packet(std::move(packet_));
  ClearPacket();
  RemoveSoftMaxPacketLength();
  delegate_->OnSerializedPacket(std::move(packet));
}

void QuicPacketCreator::ClearPacket() {
  packet_.has_ack = false;
  packet_.has_stop_waiting = false;
  packet_.has_ack_ecn = false;
  packet_.has_crypto_handshake = NOT_HANDSHAKE;
  packet_.transmission_type = NOT_RETRANSMISSION;
  packet_.encrypted_buffer = nullptr;
  packet_.encrypted_length = 0;
  packet_.has_ack_frequency = false;
  packet_.has_message = false;
  packet_.fate = SEND_TO_WRITER;
  QUIC_BUG_IF(quic_bug_12398_6, packet_.release_encrypted_buffer != nullptr)
      << ENDPOINT << "packet_.release_encrypted_buffer should be empty";
  packet_.release_encrypted_buffer = nullptr;
  QUICHE_DCHECK(packet_.retransmittable_frames.empty()) << ENDPOINT;
  QUICHE_DCHECK(packet_.nonretransmittable_frames.empty()) << ENDPOINT;
  packet_.largest_acked.Clear();
  needs_full_padding_ = false;
  packet_.bytes_not_retransmitted.reset();
  packet_.initial_header.reset();
}

size_t QuicPacketCreator::ReserializeInitialPacketInCoalescedPacket(
    const SerializedPacket& packet, size_t padding_size, char* buffer,
    size_t buffer_len) {
  QUIC_BUG_IF(quic_bug_12398_7, packet.encryption_level != ENCRYPTION_INITIAL);
  QUIC_BUG_IF(quic_bug_12398_8, packet.nonretransmittable_frames.empty() &&
                                    packet.retransmittable_frames.empty())
      << ENDPOINT
      << "Attempt to serialize empty ENCRYPTION_INITIAL packet in coalesced "
         "packet";

  if (HasPendingFrames()) {
    QUIC_BUG(quic_packet_creator_unexpected_queued_frames)
        << "Unexpected queued frames: " << GetPendingFramesInfo();
    return 0;
  }

  ScopedPacketContextSwitcher switcher(
      packet.packet_number -
          1,  // -1 because serialize packet increase packet number.
      packet.packet_number_length, packet.encryption_level, &packet_);
  for (const QuicFrame& frame : packet.nonretransmittable_frames) {
    if (!AddFrame(frame, packet.transmission_type)) {
      QUIC_BUG(quic_bug_10752_6)
          << ENDPOINT << "Failed to serialize frame: " << frame;
      return 0;
    }
  }
  for (const QuicFrame& frame : packet.retransmittable_frames) {
    if (!AddFrame(frame, packet.transmission_type)) {
      QUIC_BUG(quic_bug_10752_7)
          << ENDPOINT << "Failed to serialize frame: " << frame;
      return 0;
    }
  }
  // Add necessary padding.
  if (padding_size > 0) {
    QUIC_DVLOG(2) << ENDPOINT << "Add padding of size: " << padding_size;
    if (!AddFrame(QuicFrame(QuicPaddingFrame(padding_size)),
                  packet.transmission_type)) {
      QUIC_BUG(quic_bug_10752_8)
          << ENDPOINT << "Failed to add padding of size " << padding_size
          << " when serializing ENCRYPTION_INITIAL "
             "packet in coalesced packet";
      return 0;
    }
  }

  if (!SerializePacket(QuicOwnedPacketBuffer(buffer, nullptr), buffer_len,
                       /*allow_padding=*/false)) {
    return 0;
  }
  if (!packet.initial_header.has_value() ||
      !packet_.initial_header.has_value()) {
    QUIC_BUG(missing initial packet header)
        << "initial serialized packet does not have header populated";
  } else if (*packet.initial_header != *packet_.initial_header) {
    QUIC_BUG(initial packet header changed before reserialization)
        << ENDPOINT << "original header: " << *packet.initial_header
        << ", new header: " << *packet_.initial_header;
  }
  const size_t encrypted_length = packet_.encrypted_length;
  // Clear frames in packet_. No need to DeleteFrames since frames are owned by
  // initial_packet.
  packet_.retransmittable_frames.clear();
  packet_.nonretransmittable_frames.clear();
  ClearPacket();
  return encrypted_length;
}

void QuicPacketCreator::CreateAndSerializeStreamFrame(
    QuicStreamId id, size_t write_length, QuicStreamOffset iov_offset,
    QuicStreamOffset stream_offset, bool fin,
    TransmissionType transmission_type, size_t* num_bytes_consumed) {
  // TODO(b/167222597): consider using ScopedSerializationFailureHandler.
  QUICHE_DCHECK(queued_frames_.empty()) << ENDPOINT;
  QUICHE_DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id))
      << ENDPOINT;
  // Write out the packet header
  QuicPacketHeader header;
  FillPacketHeader(&header);
  packet_.fate = delegate_->GetSerializedPacketFate(
      /*is_mtu_discovery=*/false, packet_.encryption_level);
  QUIC_DVLOG(1) << ENDPOINT << "fate of packet " << packet_.packet_number
                << ": " << SerializedPacketFateToString(packet_.fate) << " of "
                << EncryptionLevelToString(packet_.encryption_level);

  ABSL_CACHELINE_ALIGNED char stack_buffer[kMaxOutgoingPacketSize];
  QuicOwnedPacketBuffer packet_buffer(delegate_->GetPacketBuffer());

  if (packet_buffer.buffer == nullptr) {
    packet_buffer.buffer = stack_buffer;
    packet_buffer.release_buffer = nullptr;
  }

  char* encrypted_buffer = packet_buffer.buffer;

  QuicDataWriter writer(kMaxOutgoingPacketSize, encrypted_buffer);
  size_t length_field_offset = 0;
  if (!framer_->AppendIetfPacketHeader(header, &writer, &length_field_offset)) {
    QUIC_BUG(quic_bug_10752_9) << ENDPOINT << "AppendPacketHeader failed";
    return;
  }

  // Create a Stream frame with the remaining space.
  QUIC_BUG_IF(quic_bug_12398_9, iov_offset == write_length && !fin)
      << ENDPOINT << "Creating a stream frame with no data or fin.";
  const size_t remaining_data_size = write_length - iov_offset;
  size_t min_frame_size = QuicFramer::GetMinStreamFrameSize(
      framer_->transport_version(), id, stream_offset,
      /* last_frame_in_packet= */ true, remaining_data_size);
  size_t available_size =
      max_plaintext_size_ - writer.length() - min_frame_size;
  size_t bytes_consumed = std::min<size_t>(available_size, remaining_data_size);
  size_t plaintext_bytes_written = min_frame_size + bytes_consumed;
  bool needs_padding = false;
  const size_t min_plaintext_size =
      MinPlaintextPacketSize(framer_->version(), GetPacketNumberLength());
  if (plaintext_bytes_written < min_plaintext_size) {
    needs_padding = true;
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
  if (needs_padding) {
    if (!writer.WritePaddingBytes(min_plaintext_size -
                                  plaintext_bytes_written)) {
      QUIC_BUG(quic_bug_10752_12) << ENDPOINT << "Unable to add padding bytes";
      return;
    }
    needs_padding = false;
  }
  bool omit_frame_length = !needs_padding;
  if (!framer_->AppendTypeByte(QuicFrame(frame), omit_frame_length, &writer)) {
    QUIC_BUG(quic_bug_10752_10) << ENDPOINT << "AppendTypeByte failed";
    return;
  }
  if (!framer_->AppendStreamFrame(frame, omit_frame_length, &writer)) {
    QUIC_BUG(quic_bug_10752_11) << ENDPOINT << "AppendStreamFrame failed";
    return;
  }
  if (needs_padding && plaintext_bytes_written < min_plaintext_size &&
      !writer.WritePaddingBytes(min_plaintext_size - plaintext_bytes_written)) {
    QUIC_BUG(quic_bug_10752_12) << ENDPOINT << "Unable to add padding bytes";
    return;
  }

  if (!framer_->WriteIetfLongHeaderLength(header, &writer, length_field_offset,
                                          packet_.encryption_level)) {
    return;
  }

  packet_.transmission_type = transmission_type;

  QUICHE_DCHECK(packet_.encryption_level == ENCRYPTION_FORWARD_SECURE ||
                packet_.encryption_level == ENCRYPTION_ZERO_RTT)
      << ENDPOINT << packet_.encryption_level;
  size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header),
      writer.length(), kMaxOutgoingPacketSize, encrypted_buffer);
  if (encrypted_length == 0) {
    QUIC_BUG(quic_bug_10752_13)
        << ENDPOINT << "Failed to encrypt packet number "
        << header.packet_number;
    return;
  }
  // TODO(ianswett): Optimize the storage so RetransmitableFrames can be
  // unioned with a QuicStreamFrame and a UniqueStreamBuffer.
  *num_bytes_consumed = bytes_consumed;
  packet_size_ = 0;
  packet_.encrypted_buffer = encrypted_buffer;
  packet_.encrypted_length = encrypted_length;

  packet_buffer.buffer = nullptr;
  packet_.release_encrypted_buffer = std::move(packet_buffer).release_buffer;

  packet_.retransmittable_frames.push_back(QuicFrame(frame));
  OnSerializedPacket();
}

bool QuicPacketCreator::HasPendingFrames() const {
  return !queued_frames_.empty();
}

std::string QuicPacketCreator::GetPendingFramesInfo() const {
  return QuicFramesToString(queued_frames_);
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
  if (queued_frames_.empty()) {
    return 0;
  }
  return ExpansionOnNewFrameWithLastFrame(queued_frames_.back(),
                                          framer_->transport_version());
}

// static
size_t QuicPacketCreator::ExpansionOnNewFrameWithLastFrame(
    const QuicFrame& last_frame, QuicTransportVersion version) {
  if (last_frame.type == MESSAGE_FRAME) {
    return QuicDataWriter::GetVarInt62Len(
        last_frame.message_frame->message_length);
  }
  if (last_frame.type != STREAM_FRAME) {
    return 0;
  }
  if (VersionHasIetfQuicFrames(version)) {
    return QuicDataWriter::GetVarInt62Len(last_frame.stream_frame.data_length);
  }
  return kQuicStreamPayloadLengthSize;
}

size_t QuicPacketCreator::BytesFree() const {
  return max_plaintext_size_ -
         std::min(max_plaintext_size_, PacketSize() + ExpansionOnNewFrame());
}

size_t QuicPacketCreator::BytesFreeForPadding() const {
  size_t consumed = PacketSize();
  return max_plaintext_size_ - std::min(max_plaintext_size_, consumed);
}

size_t QuicPacketCreator::PacketSize() const {
  return queued_frames_.empty() ? PacketHeaderSize() : packet_size_;
}

bool QuicPacketCreator::AddPaddedSavedFrame(
    const QuicFrame& frame, TransmissionType transmission_type) {
  if (AddFrame(frame, transmission_type)) {
    needs_full_padding_ = true;
    return true;
  }
  return false;
}

absl::optional<size_t>
QuicPacketCreator::MaybeBuildDataPacketWithChaosProtection(
    const QuicPacketHeader& header, char* buffer) {
  if (!GetQuicFlag(quic_enable_chaos_protection) ||
      framer_->perspective() != Perspective::IS_CLIENT ||
      packet_.encryption_level != ENCRYPTION_INITIAL ||
      !framer_->version().UsesCryptoFrames() || queued_frames_.size() != 2u ||
      queued_frames_[0].type != CRYPTO_FRAME ||
      queued_frames_[1].type != PADDING_FRAME ||
      // Do not perform chaos protection if we do not have a known number of
      // padding bytes to work with.
      queued_frames_[1].padding_frame.num_padding_bytes <= 0 ||
      // Chaos protection relies on the framer using a crypto data producer,
      // which is always the case in practice.
      framer_->data_producer() == nullptr) {
    return absl::nullopt;
  }
  const QuicCryptoFrame& crypto_frame = *queued_frames_[0].crypto_frame;
  if (packet_.encryption_level != crypto_frame.level) {
    QUIC_BUG(chaos frame level)
        << ENDPOINT << packet_.encryption_level << " != " << crypto_frame.level;
    return absl::nullopt;
  }
  QuicChaosProtector chaos_protector(
      crypto_frame, queued_frames_[1].padding_frame.num_padding_bytes,
      packet_size_, framer_, random_);
  return chaos_protector.BuildDataPacket(header, buffer);
}

bool QuicPacketCreator::SerializePacket(QuicOwnedPacketBuffer encrypted_buffer,
                                        size_t encrypted_buffer_len,
                                        bool allow_padding) {
  if (packet_.encrypted_buffer != nullptr) {
    const std::string error_details =
        "Packet's encrypted buffer is not empty before serialization";
    QUIC_BUG(quic_bug_10752_14) << ENDPOINT << error_details;
    delegate_->OnUnrecoverableError(QUIC_FAILED_TO_SERIALIZE_PACKET,
                                    error_details);
    return false;
  }
  ScopedSerializationFailureHandler handler(this);

  QUICHE_DCHECK_LT(0u, encrypted_buffer_len) << ENDPOINT;
  QUIC_BUG_IF(quic_bug_12398_10,
              queued_frames_.empty() && pending_padding_bytes_ == 0)
      << ENDPOINT << "Attempt to serialize empty packet";
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);
  if (packet_.encryption_level == ENCRYPTION_INITIAL) {
    packet_.initial_header = header;
  }
  if (delegate_ != nullptr) {
    packet_.fate = delegate_->GetSerializedPacketFate(
        /*is_mtu_discovery=*/QuicUtils::ContainsFrameType(queued_frames_,
                                                          MTU_DISCOVERY_FRAME),
        packet_.encryption_level);
    QUIC_DVLOG(1) << ENDPOINT << "fate of packet " << packet_.packet_number
                  << ": " << SerializedPacketFateToString(packet_.fate)
                  << " of "
                  << EncryptionLevelToString(packet_.encryption_level);
  }

  if (allow_padding) {
    MaybeAddPadding();
  }

  QUIC_DVLOG(2) << ENDPOINT << "Serializing packet " << header
                << QuicFramesToString(queued_frames_) << " at encryption_level "
                << packet_.encryption_level
                << ", allow_padding:" << allow_padding;

  if (!framer_->HasEncrypterOfEncryptionLevel(packet_.encryption_level)) {
    // TODO(fayang): Use QUIC_MISSING_WRITE_KEYS for serialization failures due
    // to missing keys.
    QUIC_BUG(quic_bug_10752_15)
        << ENDPOINT << "Attempting to serialize " << header
        << QuicFramesToString(queued_frames_) << " at missing encryption_level "
        << packet_.encryption_level << " using " << framer_->version();
    return false;
  }

  QUICHE_DCHECK_GE(max_plaintext_size_, packet_size_) << ENDPOINT;
  // Use the packet_size_ instead of the buffer size to ensure smaller
  // packet sizes are properly used.

  size_t length;
  absl::optional<size_t> length_with_chaos_protection =
      MaybeBuildDataPacketWithChaosProtection(header, encrypted_buffer.buffer);
  if (length_with_chaos_protection.has_value()) {
    length = *length_with_chaos_protection;
  } else {
    length = framer_->BuildDataPacket(header, queued_frames_,
                                      encrypted_buffer.buffer, packet_size_,
                                      packet_.encryption_level);
  }

  if (length == 0) {
    QUIC_BUG(quic_bug_10752_16)
        << ENDPOINT << "Failed to serialize "
        << QuicFramesToString(queued_frames_)
        << " at encryption_level: " << packet_.encryption_level
        << ", needs_full_padding_: " << needs_full_padding_
        << ", pending_padding_bytes_: " << pending_padding_bytes_
        << ", latched_hard_max_packet_length_: "
        << latched_hard_max_packet_length_
        << ", max_packet_length_: " << max_packet_length_
        << ", header: " << header;
    return false;
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
    QUICHE_DCHECK_EQ(packet_size_, length) << ENDPOINT;
  }
  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      encrypted_buffer_len, encrypted_buffer.buffer);
  if (encrypted_length == 0) {
    QUIC_BUG(quic_bug_10752_17)
        << ENDPOINT << "Failed to encrypt packet number "
        << packet_.packet_number;
    return false;
  }

  packet_size_ = 0;
  packet_.encrypted_buffer = encrypted_buffer.buffer;
  packet_.encrypted_length = encrypted_length;

  encrypted_buffer.buffer = nullptr;
  packet_.release_encrypted_buffer = std::move(encrypted_buffer).release_buffer;
  return true;
}

std::unique_ptr<SerializedPacket>
QuicPacketCreator::SerializeConnectivityProbingPacket() {
  QUIC_BUG_IF(quic_bug_12398_11,
              VersionHasIetfQuicFrames(framer_->transport_version()))
      << ENDPOINT
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
  QUICHE_DCHECK(length) << ENDPOINT;

  QUICHE_DCHECK_EQ(packet_.encryption_level, ENCRYPTION_FORWARD_SECURE)
      << ENDPOINT;
  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      kMaxOutgoingPacketSize, buffer.get());
  QUICHE_DCHECK(encrypted_length) << ENDPOINT;

  std::unique_ptr<SerializedPacket> serialize_packet(new SerializedPacket(
      header.packet_number, header.packet_number_length, buffer.release(),
      encrypted_length, /*has_ack=*/false, /*has_stop_waiting=*/false));

  serialize_packet->release_encrypted_buffer = [](const char* p) {
    delete[] p;
  };
  serialize_packet->encryption_level = packet_.encryption_level;
  serialize_packet->transmission_type = NOT_RETRANSMISSION;

  return serialize_packet;
}

std::unique_ptr<SerializedPacket>
QuicPacketCreator::SerializePathChallengeConnectivityProbingPacket(
    const QuicPathFrameBuffer& payload) {
  QUIC_BUG_IF(quic_bug_12398_12,
              !VersionHasIetfQuicFrames(framer_->transport_version()))
      << ENDPOINT
      << "Must be version 99 to serialize path challenge connectivity probe, "
         "is version "
      << framer_->transport_version();
  RemoveSoftMaxPacketLength();
  QuicPacketHeader header;
  // FillPacketHeader increments packet_number_.
  FillPacketHeader(&header);

  QUIC_DVLOG(2) << ENDPOINT << "Serializing path challenge packet " << header;

  std::unique_ptr<char[]> buffer(new char[kMaxOutgoingPacketSize]);
  size_t length =
      BuildPaddedPathChallengePacket(header, buffer.get(), max_plaintext_size_,
                                     payload, packet_.encryption_level);
  QUICHE_DCHECK(length) << ENDPOINT;

  QUICHE_DCHECK_EQ(packet_.encryption_level, ENCRYPTION_FORWARD_SECURE)
      << ENDPOINT;
  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      kMaxOutgoingPacketSize, buffer.get());
  QUICHE_DCHECK(encrypted_length) << ENDPOINT;

  std::unique_ptr<SerializedPacket> serialize_packet(
      new SerializedPacket(header.packet_number, header.packet_number_length,
                           buffer.release(), encrypted_length,
                           /*has_ack=*/false, /*has_stop_waiting=*/false));

  serialize_packet->release_encrypted_buffer = [](const char* p) {
    delete[] p;
  };
  serialize_packet->encryption_level = packet_.encryption_level;
  serialize_packet->transmission_type = NOT_RETRANSMISSION;

  return serialize_packet;
}

std::unique_ptr<SerializedPacket>
QuicPacketCreator::SerializePathResponseConnectivityProbingPacket(
    const quiche::QuicheCircularDeque<QuicPathFrameBuffer>& payloads,
    const bool is_padded) {
  QUIC_BUG_IF(quic_bug_12398_13,
              !VersionHasIetfQuicFrames(framer_->transport_version()))
      << ENDPOINT
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
  QUICHE_DCHECK(length) << ENDPOINT;

  QUICHE_DCHECK_EQ(packet_.encryption_level, ENCRYPTION_FORWARD_SECURE)
      << ENDPOINT;
  const size_t encrypted_length = framer_->EncryptInPlace(
      packet_.encryption_level, packet_.packet_number,
      GetStartOfEncryptedData(framer_->transport_version(), header), length,
      kMaxOutgoingPacketSize, buffer.get());
  QUICHE_DCHECK(encrypted_length) << ENDPOINT;

  std::unique_ptr<SerializedPacket> serialize_packet(
      new SerializedPacket(header.packet_number, header.packet_number_length,
                           buffer.release(), encrypted_length,
                           /*has_ack=*/false, /*has_stop_waiting=*/false));

  serialize_packet->release_encrypted_buffer = [](const char* p) {
    delete[] p;
  };
  serialize_packet->encryption_level = packet_.encryption_level;
  serialize_packet->transmission_type = NOT_RETRANSMISSION;

  return serialize_packet;
}

size_t QuicPacketCreator::BuildPaddedPathChallengePacket(
    const QuicPacketHeader& header, char* buffer, size_t packet_length,
    const QuicPathFrameBuffer& payload, EncryptionLevel level) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(framer_->transport_version()))
      << ENDPOINT;
  QuicFrames frames;

  // Write a PATH_CHALLENGE frame, which has a random 8-byte payload
  frames.push_back(QuicFrame(QuicPathChallengeFrame(0, payload)));

  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnFrameAddedToPacket(frames.back());
  }

  // Add padding to the rest of the packet in order to assess Path MTU
  // characteristics.
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(padding_frame));

  return framer_->BuildDataPacket(header, frames, buffer, packet_length, level);
}

size_t QuicPacketCreator::BuildPathResponsePacket(
    const QuicPacketHeader& header, char* buffer, size_t packet_length,
    const quiche::QuicheCircularDeque<QuicPathFrameBuffer>& payloads,
    const bool is_padded, EncryptionLevel level) {
  if (payloads.empty()) {
    QUIC_BUG(quic_bug_12398_14)
        << ENDPOINT
        << "Attempt to generate connectivity response with no request payloads";
    return 0;
  }
  QUICHE_DCHECK(VersionHasIetfQuicFrames(framer_->transport_version()))
      << ENDPOINT;

  QuicFrames frames;
  for (const QuicPathFrameBuffer& payload : payloads) {
    // Note that the control frame ID can be 0 since this is not retransmitted.
    frames.push_back(QuicFrame(QuicPathResponseFrame(0, payload)));
    if (debug_delegate_ != nullptr) {
      debug_delegate_->OnFrameAddedToPacket(frames.back());
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
    const QuicPacketHeader& header, char* buffer, size_t packet_length,
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
    const QuicCoalescedPacket& coalesced, char* buffer, size_t buffer_len) {
  if (HasPendingFrames()) {
    QUIC_BUG(quic_bug_10752_18)
        << ENDPOINT << "Try to serialize coalesced packet with pending frames";
    return 0;
  }
  RemoveSoftMaxPacketLength();
  QUIC_BUG_IF(quic_bug_12398_15, coalesced.length() == 0)
      << ENDPOINT << "Attempt to serialize empty coalesced packet";
  size_t packet_length = 0;
  size_t initial_length = 0;
  size_t padding_size = 0;
  if (coalesced.initial_packet() != nullptr) {
    // Padding coalesced packet containing initial packet to full.
    padding_size = coalesced.max_packet_length() - coalesced.length();
    if (framer_->perspective() == Perspective::IS_SERVER &&
        QuicUtils::ContainsFrameType(
            coalesced.initial_packet()->retransmittable_frames,
            CONNECTION_CLOSE_FRAME)) {
      // Do not pad server initial connection close packet.
      padding_size = 0;
    }
    initial_length = ReserializeInitialPacketInCoalescedPacket(
        *coalesced.initial_packet(), padding_size, buffer, buffer_len);
    if (initial_length == 0) {
      QUIC_BUG(quic_bug_10752_19)
          << ENDPOINT
          << "Failed to reserialize ENCRYPTION_INITIAL packet in "
             "coalesced packet";
      return 0;
    }
    QUIC_BUG_IF(quic_reserialize_initial_packet_unexpected_size,
                coalesced.initial_packet()->encrypted_length + padding_size !=
                    initial_length)
        << "Reserialize initial packet in coalescer has unexpected size, "
           "original_length: "
        << coalesced.initial_packet()->encrypted_length
        << ", coalesced.max_packet_length: " << coalesced.max_packet_length()
        << ", coalesced.length: " << coalesced.length()
        << ", padding_size: " << padding_size
        << ", serialized_length: " << initial_length
        << ", retransmittable frames: "
        << QuicFramesToString(
               coalesced.initial_packet()->retransmittable_frames)
        << ", nonretransmittable frames: "
        << QuicFramesToString(
               coalesced.initial_packet()->nonretransmittable_frames);
    buffer += initial_length;
    buffer_len -= initial_length;
    packet_length += initial_length;
  }
  size_t length_copied = 0;
  if (!coalesced.CopyEncryptedBuffers(buffer, buffer_len, &length_copied)) {
    QUIC_BUG(quic_serialize_coalesced_packet_copy_failure)
        << "SerializeCoalescedPacket failed. buffer_len:" << buffer_len
        << ", initial_length:" << initial_length
        << ", padding_size: " << padding_size
        << ", length_copied:" << length_copied
        << ", coalesced.length:" << coalesced.length()
        << ", coalesced.max_packet_length:" << coalesced.max_packet_length()
        << ", coalesced.packet_lengths:"
        << absl::StrJoin(coalesced.packet_lengths(), ":");
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

uint8_t QuicPacketCreator::GetDestinationConnectionIdLength() const {
  QUICHE_DCHECK(QuicUtils::IsConnectionIdValidForVersion(server_connection_id_,
                                                         transport_version()))
      << ENDPOINT;
  return GetDestinationConnectionIdIncluded() == CONNECTION_ID_PRESENT
             ? GetDestinationConnectionId().length()
             : 0;
}

uint8_t QuicPacketCreator::GetSourceConnectionIdLength() const {
  QUICHE_DCHECK(QuicUtils::IsConnectionIdValidForVersion(server_connection_id_,
                                                         transport_version()))
      << ENDPOINT;
  return GetSourceConnectionIdIncluded() == CONNECTION_ID_PRESENT
             ? GetSourceConnectionId().length()
             : 0;
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

quiche::QuicheVariableLengthIntegerLength
QuicPacketCreator::GetRetryTokenLengthLength() const {
  if (QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      HasIetfLongHeader() &&
      EncryptionlevelToLongHeaderType(packet_.encryption_level) == INITIAL) {
    return QuicDataWriter::GetVarInt62Len(GetRetryToken().length());
  }
  return quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
}

absl::string_view QuicPacketCreator::GetRetryToken() const {
  if (QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      HasIetfLongHeader() &&
      EncryptionlevelToLongHeaderType(packet_.encryption_level) == INITIAL) {
    return retry_token_;
  }
  return absl::string_view();
}

void QuicPacketCreator::SetRetryToken(absl::string_view retry_token) {
  retry_token_ = std::string(retry_token);
}

bool QuicPacketCreator::ConsumeRetransmittableControlFrame(
    const QuicFrame& frame) {
  QUIC_BUG_IF(quic_bug_12398_16, IsControlFrame(frame.type) &&
                                     !GetControlFrameId(frame) &&
                                     frame.type != PING_FRAME)
      << ENDPOINT
      << "Adding a control frame with no control frame id: " << frame;
  QUICHE_DCHECK(QuicUtils::IsRetransmittableFrame(frame.type))
      << ENDPOINT << frame;
  MaybeBundleOpportunistically();
  if (HasPendingFrames()) {
    if (AddFrame(frame, next_transmission_type_)) {
      // There is pending frames and current frame fits.
      return true;
    }
  }
  QUICHE_DCHECK(!HasPendingFrames()) << ENDPOINT;
  if (frame.type != PING_FRAME && frame.type != CONNECTION_CLOSE_FRAME &&
      !delegate_->ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA,
                                       NOT_HANDSHAKE)) {
    // Do not check congestion window for ping or connection close frames.
    return false;
  }
  const bool success = AddFrame(frame, next_transmission_type_);
  QUIC_BUG_IF(quic_bug_10752_20, !success)
      << ENDPOINT << "Failed to add frame:" << frame
      << " transmission_type:" << next_transmission_type_;
  return success;
}

QuicConsumedData QuicPacketCreator::ConsumeData(QuicStreamId id,
                                                size_t write_length,
                                                QuicStreamOffset offset,
                                                StreamSendingState state) {
  QUIC_BUG_IF(quic_bug_10752_21, !flusher_attached_)
      << ENDPOINT
      << "Packet flusher is not attached when "
         "generator tries to write stream data.";
  bool has_handshake = QuicUtils::IsCryptoStreamId(transport_version(), id);
  MaybeBundleOpportunistically();
  bool fin = state != NO_FIN;
  QUIC_BUG_IF(quic_bug_12398_17, has_handshake && fin)
      << ENDPOINT << "Handshake packets should never send a fin";
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
    QUIC_BUG(quic_bug_10752_22)
        << ENDPOINT << "Attempt to consume empty data without FIN.";
    return QuicConsumedData(0, false);
  }
  // We determine if we can enter the fast path before executing
  // the slow path loop.
  bool run_fast_path =
      !has_handshake && state != FIN_AND_PADDING && !HasPendingFrames() &&
      write_length - total_bytes_consumed > kMaxOutgoingPacketSize &&
      latched_hard_max_packet_length_ == 0;

  while (!run_fast_path &&
         (has_handshake || delegate_->ShouldGeneratePacket(
                               HAS_RETRANSMITTABLE_DATA, NOT_HANDSHAKE))) {
    QuicFrame frame;
    bool needs_full_padding =
        has_handshake && fully_pad_crypto_handshake_packets_;

    if (!ConsumeDataToFillCurrentPacket(id, write_length - total_bytes_consumed,
                                        offset + total_bytes_consumed, fin,
                                        needs_full_padding,
                                        next_transmission_type_, &frame)) {
      // The creator is always flushed if there's not enough room for a new
      // stream frame before ConsumeData, so ConsumeData should always succeed.
      QUIC_BUG(quic_bug_10752_23)
          << ENDPOINT << "Failed to ConsumeData, stream:" << id;
      return QuicConsumedData(0, false);
    }

    // A stream frame is created and added.
    size_t bytes_consumed = frame.stream_frame.data_length;
    total_bytes_consumed += bytes_consumed;
    fin_consumed = fin && total_bytes_consumed == write_length;
    if (fin_consumed && state == FIN_AND_PADDING) {
      AddRandomPadding();
    }
    QUICHE_DCHECK(total_bytes_consumed == write_length ||
                  (bytes_consumed > 0 && HasPendingFrames()))
        << ENDPOINT;

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
    QuicStreamId id, size_t write_length, QuicStreamOffset offset, bool fin,
    size_t total_bytes_consumed) {
  QUICHE_DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id))
      << ENDPOINT;
  if (AttemptingToSendUnencryptedStreamData()) {
    return QuicConsumedData(total_bytes_consumed,
                            fin && (total_bytes_consumed == write_length));
  }

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
      QUIC_BUG(quic_bug_10752_24) << ENDPOINT << error_details;
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
  QUIC_DVLOG(2) << ENDPOINT << "ConsumeCryptoData " << level << " write_length "
                << write_length << " offset " << offset;
  QUIC_BUG_IF(quic_bug_10752_25, !flusher_attached_)
      << ENDPOINT
      << "Packet flusher is not attached when "
         "generator tries to write crypto data.";
  MaybeBundleOpportunistically();
  // To make reasoning about crypto frames easier, we don't combine them with
  // other retransmittable frames in a single packet.
  // TODO(nharper): Once we have separate packet number spaces, everything
  // should be driven by encryption level, and we should stop flushing in this
  // spot.
  if (HasPendingRetransmittableFrames()) {
    FlushCurrentPacket();
  }

  size_t total_bytes_consumed = 0;

  while (
      total_bytes_consumed < write_length &&
      delegate_->ShouldGeneratePacket(HAS_RETRANSMITTABLE_DATA, IS_HANDSHAKE)) {
    QuicFrame frame;
    if (!ConsumeCryptoDataToFillCurrentPacket(
            level, write_length - total_bytes_consumed,
            offset + total_bytes_consumed, fully_pad_crypto_handshake_packets_,
            next_transmission_type_, &frame)) {
      // The only pending data in the packet is non-retransmittable frames.
      // I'm assuming here that they won't occupy so much of the packet that a
      // CRYPTO frame won't fit.
      QUIC_BUG_IF(quic_bug_10752_26, !HasSoftMaxPacketLength()) << absl::StrCat(
          ENDPOINT, "Failed to ConsumeCryptoData at level ", level,
          ", pending_frames: ", GetPendingFramesInfo(),
          ", has_soft_max_packet_length: ", HasSoftMaxPacketLength(),
          ", max_packet_length: ", max_packet_length_, ", transmission_type: ",
          TransmissionTypeToString(next_transmission_type_),
          ", packet_number: ", packet_number().ToString());
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
    QUIC_BUG(quic_bug_10752_27)
        << ENDPOINT
        << "MTU discovery packets should only be sent when no other "
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
  QUIC_BUG_IF(quic_bug_10752_28, !success)
      << ENDPOINT << "Failed to send path MTU target_mtu:" << target_mtu
      << " transmission_type:" << next_transmission_type_;

  // Reset the packet length back.
  SetMaxPacketLength(current_mtu);
}

void QuicPacketCreator::MaybeBundleOpportunistically() {
  if (flush_ack_in_maybe_bundle_) {
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_flush_ack_in_maybe_bundle, 1, 3);
    delegate_->MaybeBundleOpportunistically();
    return;
  }
  if (has_ack()) {
    // Ack already queued, nothing to do.
    return;
  }
  if (!delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                       NOT_HANDSHAKE)) {
    return;
  }
  const bool flushed = FlushAckFrame(delegate_->MaybeBundleOpportunistically());
  QUIC_BUG_IF(quic_bug_10752_29, !flushed)
      << ENDPOINT << "Failed to flush ACK frame. encryption_level:"
      << packet_.encryption_level;
}

bool QuicPacketCreator::FlushAckFrame(const QuicFrames& frames) {
  QUIC_BUG_IF(quic_bug_10752_30, !flusher_attached_)
      << ENDPOINT
      << "Packet flusher is not attached when "
         "generator tries to send ACK frame.";
  // MaybeBundleOpportunistically could be called nestedly when
  // sending a control frame causing another control frame to be sent.
  QUIC_BUG_IF(quic_bug_12398_18, !frames.empty() && has_ack())
      << ENDPOINT << "Trying to flush " << quiche::PrintElements(frames)
      << " when there is ACK queued";
  for (const auto& frame : frames) {
    QUICHE_DCHECK(frame.type == ACK_FRAME || frame.type == STOP_WAITING_FRAME)
        << ENDPOINT;
    if (HasPendingFrames()) {
      if (AddFrame(frame, next_transmission_type_)) {
        // There is pending frames and current frame fits.
        continue;
      }
    }
    QUICHE_DCHECK(!HasPendingFrames()) << ENDPOINT;
    // There is no pending frames, consult the delegate whether a packet can be
    // generated.
    if (!delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                         NOT_HANDSHAKE)) {
      return false;
    }
    const bool success = AddFrame(frame, next_transmission_type_);
    QUIC_BUG_IF(quic_bug_10752_31, !success)
        << ENDPOINT << "Failed to flush " << frame;
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
  if (GetQuicFlag(quic_export_write_path_stats_at_server)) {
    if (!write_start_packet_number_.IsInitialized()) {
      QUIC_BUG(quic_bug_10752_32)
          << ENDPOINT << "write_start_packet_number is not initialized";
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

MessageStatus QuicPacketCreator::AddMessageFrame(
    QuicMessageId message_id, absl::Span<quiche::QuicheMemSlice> message) {
  QUIC_BUG_IF(quic_bug_10752_33, !flusher_attached_)
      << ENDPOINT
      << "Packet flusher is not attached when "
         "generator tries to add message frame.";
  MaybeBundleOpportunistically();
  const QuicByteCount message_length = MemSliceSpanTotalSize(message);
  if (message_length > GetCurrentLargestMessagePayload()) {
    return MESSAGE_STATUS_TOO_LARGE;
  }
  if (!HasRoomForMessageFrame(message_length)) {
    FlushCurrentPacket();
  }
  QuicMessageFrame* frame = new QuicMessageFrame(message_id, message);
  const bool success = AddFrame(QuicFrame(frame), next_transmission_type_);
  if (!success) {
    QUIC_BUG(quic_bug_10752_34)
        << ENDPOINT << "Failed to send message " << message_id;
    delete frame;
    return MESSAGE_STATUS_INTERNAL_ERROR;
  }
  QUICHE_DCHECK_EQ(MemSliceSpanTotalSize(message),
                   0u);  // Ensure the old slices are empty.
  return MESSAGE_STATUS_SUCCESS;
}

quiche::QuicheVariableLengthIntegerLength QuicPacketCreator::GetLengthLength()
    const {
  if (QuicVersionHasLongHeaderLengths(framer_->transport_version()) &&
      HasIetfLongHeader()) {
    QuicLongHeaderType long_header_type =
        EncryptionlevelToLongHeaderType(packet_.encryption_level);
    if (long_header_type == INITIAL || long_header_type == ZERO_RTT_PROTECTED ||
        long_header_type == HANDSHAKE) {
      return quiche::VARIABLE_LENGTH_INTEGER_LENGTH_2;
    }
  }
  return quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
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
    QUICHE_DCHECK_EQ(Perspective::IS_SERVER, framer_->perspective())
        << ENDPOINT;
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

size_t QuicPacketCreator::GetSerializedFrameLength(const QuicFrame& frame) {
  size_t serialized_frame_length = framer_->GetSerializedFrameLength(
      frame, BytesFree(), queued_frames_.empty(),
      /* last_frame_in_packet= */ true, GetPacketNumberLength());
  if (!framer_->version().HasHeaderProtection() ||
      serialized_frame_length == 0) {
    return serialized_frame_length;
  }
  // Calculate frame bytes and bytes free with this frame added.
  const size_t frame_bytes = PacketSize() - PacketHeaderSize() +
                             ExpansionOnNewFrame() + serialized_frame_length;
  if (frame_bytes >=
      MinPlaintextPacketSize(framer_->version(), GetPacketNumberLength())) {
    // No extra bytes is needed.
    return serialized_frame_length;
  }
  if (BytesFree() < serialized_frame_length) {
    QUIC_BUG(quic_bug_10752_35) << ENDPOINT << "Frame does not fit: " << frame;
    return 0;
  }
  // Please note bytes_free does not take |frame|'s expansion into account.
  size_t bytes_free = BytesFree() - serialized_frame_length;
  // Extra bytes needed (this is NOT padding needed) should be at least 1
  // padding + expansion.
  const size_t extra_bytes_needed = std::max(
      1 + ExpansionOnNewFrameWithLastFrame(frame, framer_->transport_version()),
      MinPlaintextPacketSize(framer_->version(), GetPacketNumberLength()) -
          frame_bytes);
  if (bytes_free < extra_bytes_needed) {
    // This frame does not fit.
    return 0;
  }
  return serialized_frame_length;
}

bool QuicPacketCreator::AddFrame(const QuicFrame& frame,
                                 TransmissionType transmission_type) {
  QUIC_DVLOG(1) << ENDPOINT << "Adding frame with transmission type "
                << transmission_type << ": " << frame;
  if (frame.type == STREAM_FRAME &&
      !QuicUtils::IsCryptoStreamId(framer_->transport_version(),
                                   frame.stream_frame.stream_id) &&
      AttemptingToSendUnencryptedStreamData()) {
    return false;
  }

  // Sanity check to ensure we don't send frames at the wrong encryption level.
  QUICHE_DCHECK(
      packet_.encryption_level == ENCRYPTION_ZERO_RTT ||
      packet_.encryption_level == ENCRYPTION_FORWARD_SECURE ||
      (frame.type != GOAWAY_FRAME && frame.type != WINDOW_UPDATE_FRAME &&
       frame.type != HANDSHAKE_DONE_FRAME &&
       frame.type != NEW_CONNECTION_ID_FRAME &&
       frame.type != MAX_STREAMS_FRAME && frame.type != STREAMS_BLOCKED_FRAME &&
       frame.type != PATH_RESPONSE_FRAME &&
       frame.type != PATH_CHALLENGE_FRAME && frame.type != STOP_SENDING_FRAME &&
       frame.type != MESSAGE_FRAME && frame.type != NEW_TOKEN_FRAME &&
       frame.type != RETIRE_CONNECTION_ID_FRAME &&
       frame.type != ACK_FREQUENCY_FRAME))
      << ENDPOINT << frame.type << " not allowed at "
      << packet_.encryption_level;

  if (frame.type == STREAM_FRAME) {
    if (MaybeCoalesceStreamFrame(frame.stream_frame)) {
      LogCoalesceStreamFrameStatus(true);
      return true;
    } else {
      LogCoalesceStreamFrameStatus(false);
    }
  }

  // If this is an ACK frame, validate that it is non-empty and that
  // largest_acked matches the max packet number.
  QUICHE_DCHECK(frame.type != ACK_FRAME || (!frame.ack_frame->packets.Empty() &&
                                            frame.ack_frame->packets.Max() ==
                                                frame.ack_frame->largest_acked))
      << ENDPOINT << "Invalid ACK frame: " << frame;

  size_t frame_len = GetSerializedFrameLength(frame);
  if (frame_len == 0 && RemoveSoftMaxPacketLength()) {
    // Remove soft max_packet_length and retry.
    frame_len = GetSerializedFrameLength(frame);
  }
  if (frame_len == 0) {
    QUIC_DVLOG(1) << ENDPOINT
                  << "Flushing because current open packet is full when adding "
                  << frame;
    FlushCurrentPacket();
    return false;
  }
  if (queued_frames_.empty()) {
    packet_size_ = PacketHeaderSize();
  }
  QUICHE_DCHECK_LT(0u, packet_size_) << ENDPOINT;

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
    if (frame.ack_frame->ecn_counters.has_value()) {
      packet_.has_ack_ecn = true;
    }
  } else if (frame.type == STOP_WAITING_FRAME) {
    packet_.has_stop_waiting = true;
  } else if (frame.type == ACK_FREQUENCY_FRAME) {
    packet_.has_ack_frequency = true;
  } else if (frame.type == MESSAGE_FRAME) {
    packet_.has_message = true;
  }
  if (debug_delegate_ != nullptr) {
    debug_delegate_->OnFrameAddedToPacket(frame);
  }

  if (transmission_type == NOT_RETRANSMISSION) {
    packet_.bytes_not_retransmitted.emplace(
        packet_.bytes_not_retransmitted.value_or(0) + frame_len);
  } else if (QuicUtils::IsRetransmittableFrame(frame.type)) {
    // Packet transmission type is determined by the last added retransmittable
    // frame of a retransmission type. If a packet has no retransmittable
    // retransmission frames, it has type NOT_RETRANSMISSION.
    packet_.transmission_type = transmission_type;
  }
  return true;
}

void QuicPacketCreator::MaybeAddExtraPaddingForHeaderProtection() {
  if (!framer_->version().HasHeaderProtection() || needs_full_padding_) {
    return;
  }
  const size_t frame_bytes = PacketSize() - PacketHeaderSize();
  if (frame_bytes >=
      MinPlaintextPacketSize(framer_->version(), GetPacketNumberLength())) {
    return;
  }
  QuicByteCount min_header_protection_padding =
      MinPlaintextPacketSize(framer_->version(), GetPacketNumberLength()) -
      frame_bytes;
  // Update pending_padding_bytes_.
  pending_padding_bytes_ =
      std::max(pending_padding_bytes_, min_header_protection_padding);
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
  QUICHE_DCHECK_EQ(packet_.retransmittable_frames.back().type, STREAM_FRAME)
      << ENDPOINT;
  QuicStreamFrame* retransmittable =
      &packet_.retransmittable_frames.back().stream_frame;
  QUICHE_DCHECK_EQ(retransmittable->stream_id, frame.stream_id) << ENDPOINT;
  QUICHE_DCHECK_EQ(retransmittable->offset + retransmittable->data_length,
                   frame.offset)
      << ENDPOINT;
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
  QUIC_DVLOG(1) << ENDPOINT << "Restoring max packet length to: "
                << latched_hard_max_packet_length_;
  SetMaxPacketLength(latched_hard_max_packet_length_);
  // Reset latched_max_packet_length_.
  latched_hard_max_packet_length_ = 0;
  return true;
}

void QuicPacketCreator::MaybeAddPadding() {
  // The current packet should have no padding bytes because padding is only
  // added when this method is called just before the packet is serialized.
  if (BytesFreeForPadding() == 0) {
    // Don't pad full packets.
    return;
  }

  if (packet_.fate == COALESCE) {
    // Do not add full padding if the packet is going to be coalesced.
    needs_full_padding_ = false;
  }

  // Header protection requires a minimum plaintext packet size.
  MaybeAddExtraPaddingForHeaderProtection();

  QUIC_DVLOG(3) << "MaybeAddPadding for " << packet_.packet_number
                << ": transmission_type:" << packet_.transmission_type
                << ", fate:" << packet_.fate
                << ", needs_full_padding_:" << needs_full_padding_
                << ", pending_padding_bytes_:" << pending_padding_bytes_
                << ", BytesFree:" << BytesFree();

  if (!needs_full_padding_ && pending_padding_bytes_ == 0) {
    // Do not need padding.
    return;
  }

  int padding_bytes = -1;
  if (!needs_full_padding_) {
    padding_bytes =
        std::min<int16_t>(pending_padding_bytes_, BytesFreeForPadding());
    pending_padding_bytes_ -= padding_bytes;
  }

  if (!queued_frames_.empty()) {
    // Insert PADDING before the other frames to avoid adding a length field
    // to any trailing STREAM frame.
    if (needs_full_padding_) {
      padding_bytes = BytesFreeForPadding();
    }
    // AddFrame cannot be used here because it adds the frame to the end of the
    // packet.
    QuicFrame frame{QuicPaddingFrame(padding_bytes)};
    queued_frames_.insert(queued_frames_.begin(), frame);
    packet_size_ += padding_bytes;
    packet_.nonretransmittable_frames.push_back(frame);
    if (packet_.transmission_type == NOT_RETRANSMISSION) {
      packet_.bytes_not_retransmitted.emplace(
          packet_.bytes_not_retransmitted.value_or(0) + padding_bytes);
    }
  } else {
    bool success = AddFrame(QuicFrame(QuicPaddingFrame(padding_bytes)),
                            packet_.transmission_type);
    QUIC_BUG_IF(quic_bug_10752_36, !success)
        << ENDPOINT << "Failed to add padding_bytes: " << padding_bytes
        << " transmission_type: " << packet_.transmission_type;
  }
}

bool QuicPacketCreator::IncludeNonceInPublicHeader() const {
  return have_diversification_nonce_ &&
         packet_.encryption_level == ENCRYPTION_ZERO_RTT;
}

bool QuicPacketCreator::IncludeVersionInHeader() const {
  return packet_.encryption_level < ENCRYPTION_FORWARD_SECURE;
}

void QuicPacketCreator::AddPendingPadding(QuicByteCount size) {
  pending_padding_bytes_ += size;
  QUIC_DVLOG(3) << "After AddPendingPadding(" << size
                << "), pending_padding_bytes_:" << pending_padding_bytes_;
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
  QUICHE_DCHECK(server_connection_id_included == CONNECTION_ID_PRESENT ||
                server_connection_id_included == CONNECTION_ID_ABSENT)
      << ENDPOINT;
  QUICHE_DCHECK(framer_->perspective() == Perspective::IS_SERVER ||
                server_connection_id_included != CONNECTION_ID_ABSENT)
      << ENDPOINT;
  server_connection_id_included_ = server_connection_id_included;
}

void QuicPacketCreator::SetServerConnectionId(
    QuicConnectionId server_connection_id) {
  server_connection_id_ = server_connection_id;
}

void QuicPacketCreator::SetClientConnectionId(
    QuicConnectionId client_connection_id) {
  QUICHE_DCHECK(client_connection_id.IsEmpty() ||
                framer_->version().SupportsClientConnectionIds())
      << ENDPOINT;
  client_connection_id_ = client_connection_id;
}

QuicPacketLength QuicPacketCreator::GetCurrentLargestMessagePayload() const {
  const size_t packet_header_size = GetPacketHeaderSize(
      framer_->transport_version(), GetDestinationConnectionIdLength(),
      GetSourceConnectionIdLength(), IncludeVersionInHeader(),
      IncludeNonceInPublicHeader(), GetPacketNumberLength(),
      // No Retry token on packets containing application data.
      quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0, 0, GetLengthLength());
  // This is the largest possible message payload when the length field is
  // omitted.
  size_t max_plaintext_size =
      latched_hard_max_packet_length_ == 0
          ? max_plaintext_size_
          : framer_->GetMaxPlaintextSize(latched_hard_max_packet_length_);
  size_t largest_frame =
      max_plaintext_size - std::min(max_plaintext_size, packet_header_size);
  if (static_cast<QuicByteCount>(largest_frame) > max_datagram_frame_size_) {
    largest_frame = static_cast<size_t>(max_datagram_frame_size_);
  }
  return largest_frame - std::min(largest_frame, kQuicFrameTypeSize);
}

QuicPacketLength QuicPacketCreator::GetGuaranteedLargestMessagePayload() const {
  // QUIC Crypto server packets may include a diversification nonce.
  const bool may_include_nonce =
      framer_->version().handshake_protocol == PROTOCOL_QUIC_CRYPTO &&
      framer_->perspective() == Perspective::IS_SERVER;
  // IETF QUIC long headers include a length on client 0RTT packets.
  quiche::QuicheVariableLengthIntegerLength length_length =
      quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
  if (framer_->perspective() == Perspective::IS_CLIENT) {
    length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_2;
  }
  if (!QuicVersionHasLongHeaderLengths(framer_->transport_version())) {
    length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
  }
  const size_t packet_header_size = GetPacketHeaderSize(
      framer_->transport_version(), GetDestinationConnectionIdLength(),
      // Assume CID lengths don't change, but version may be present.
      GetSourceConnectionIdLength(), kIncludeVersion, may_include_nonce,
      PACKET_4BYTE_PACKET_NUMBER,
      // No Retry token on packets containing application data.
      quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0, 0, length_length);
  // This is the largest possible message payload when the length field is
  // omitted.
  size_t max_plaintext_size =
      latched_hard_max_packet_length_ == 0
          ? max_plaintext_size_
          : framer_->GetMaxPlaintextSize(latched_hard_max_packet_length_);
  size_t largest_frame =
      max_plaintext_size - std::min(max_plaintext_size, packet_header_size);
  if (static_cast<QuicByteCount>(largest_frame) > max_datagram_frame_size_) {
    largest_frame = static_cast<size_t>(max_datagram_frame_size_);
  }
  const QuicPacketLength largest_payload =
      largest_frame - std::min(largest_frame, kQuicFrameTypeSize);
  // This must always be less than or equal to GetCurrentLargestMessagePayload.
  QUICHE_DCHECK_LE(largest_payload, GetCurrentLargestMessagePayload())
      << ENDPOINT;
  return largest_payload;
}

bool QuicPacketCreator::AttemptingToSendUnencryptedStreamData() {
  if (packet_.encryption_level == ENCRYPTION_ZERO_RTT ||
      packet_.encryption_level == ENCRYPTION_FORWARD_SECURE) {
    return false;
  }
  const std::string error_details =
      absl::StrCat("Cannot send stream data with level: ",
                   EncryptionLevelToString(packet_.encryption_level));
  QUIC_BUG(quic_bug_10752_37) << ENDPOINT << error_details;
  delegate_->OnUnrecoverableError(QUIC_ATTEMPT_TO_SEND_UNENCRYPTED_STREAM_DATA,
                                  error_details);
  return true;
}

bool QuicPacketCreator::HasIetfLongHeader() const {
  return packet_.encryption_level < ENCRYPTION_FORWARD_SECURE;
}

// static
size_t QuicPacketCreator::MinPlaintextPacketSize(
    const ParsedQuicVersion& version,
    QuicPacketNumberLength packet_number_length) {
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
  return (version.UsesTls() ? 4 : 8) - packet_number_length;
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

bool QuicPacketCreator::HasSoftMaxPacketLength() const {
  return latched_hard_max_packet_length_ != 0;
}

void QuicPacketCreator::SetDefaultPeerAddress(QuicSocketAddress address) {
  if (!packet_.peer_address.IsInitialized()) {
    packet_.peer_address = address;
    return;
  }
  if (packet_.peer_address != address) {
    FlushCurrentPacket();
    packet_.peer_address = address;
  }
}

#define ENDPOINT2                                                          \
  (creator_->framer_->perspective() == Perspective::IS_SERVER ? "Server: " \
                                                              : "Client: ")

QuicPacketCreator::ScopedPeerAddressContext::ScopedPeerAddressContext(
    QuicPacketCreator* creator, QuicSocketAddress address,
    const QuicConnectionId& client_connection_id,
    const QuicConnectionId& server_connection_id)
    : creator_(creator),
      old_peer_address_(creator_->packet_.peer_address),
      old_client_connection_id_(creator_->GetClientConnectionId()),
      old_server_connection_id_(creator_->GetServerConnectionId()) {
  QUIC_BUG_IF(quic_bug_12398_19, !old_peer_address_.IsInitialized())
      << ENDPOINT2
      << "Context is used before serialized packet's peer address is "
         "initialized.";
  creator_->SetDefaultPeerAddress(address);
  if (creator_->version().HasIetfQuicFrames()) {
    // Flush current packet if connection ID length changes.
    if (address == old_peer_address_ &&
        ((client_connection_id.length() !=
          old_client_connection_id_.length()) ||
         (server_connection_id.length() !=
          old_server_connection_id_.length()))) {
      creator_->FlushCurrentPacket();
    }
    creator_->SetClientConnectionId(client_connection_id);
    creator_->SetServerConnectionId(server_connection_id);
  }
}

QuicPacketCreator::ScopedPeerAddressContext::~ScopedPeerAddressContext() {
  creator_->SetDefaultPeerAddress(old_peer_address_);
  if (creator_->version().HasIetfQuicFrames()) {
    creator_->SetClientConnectionId(old_client_connection_id_);
    creator_->SetServerConnectionId(old_server_connection_id_);
  }
}

QuicPacketCreator::ScopedSerializationFailureHandler::
    ScopedSerializationFailureHandler(QuicPacketCreator* creator)
    : creator_(creator) {}

QuicPacketCreator::ScopedSerializationFailureHandler::
    ~ScopedSerializationFailureHandler() {
  if (creator_ == nullptr) {
    return;
  }
  // Always clear queued_frames_.
  creator_->queued_frames_.clear();

  if (creator_->packet_.encrypted_buffer == nullptr) {
    const std::string error_details = "Failed to SerializePacket.";
    QUIC_BUG(quic_bug_10752_38) << ENDPOINT2 << error_details;
    creator_->delegate_->OnUnrecoverableError(QUIC_FAILED_TO_SERIALIZE_PACKET,
                                              error_details);
  }
}

#undef ENDPOINT2

void QuicPacketCreator::set_encryption_level(EncryptionLevel level) {
  QUICHE_DCHECK(level == packet_.encryption_level || !HasPendingFrames())
      << ENDPOINT << "Cannot update encryption level from "
      << packet_.encryption_level << " to " << level
      << " when we already have pending frames: "
      << QuicFramesToString(queued_frames_);
  packet_.encryption_level = level;
}

void QuicPacketCreator::AddPathChallengeFrame(
    const QuicPathFrameBuffer& payload) {
  // TODO(danzh) Unify similar checks at several entry points into one in
  // AddFrame(). Sort out test helper functions and peer class that don't
  // enforce this check.
  QUIC_BUG_IF(quic_bug_10752_39, !flusher_attached_)
      << ENDPOINT
      << "Packet flusher is not attached when "
         "generator tries to write stream data.";
  // Write a PATH_CHALLENGE frame, which has a random 8-byte payload.
  QuicFrame frame(QuicPathChallengeFrame(0, payload));
  if (AddPaddedFrameWithRetry(frame)) {
    return;
  }
  // Fail silently if the probing packet cannot be written, path validation
  // initiator will retry sending automatically.
  // TODO(danzh) This will consume retry budget, if it causes performance
  // regression, consider to notify the caller about the sending failure and let
  // the caller to decide if it worth retrying.
  QUIC_DVLOG(1) << ENDPOINT << "Can't send PATH_CHALLENGE now";
}

bool QuicPacketCreator::AddPathResponseFrame(
    const QuicPathFrameBuffer& data_buffer) {
  QuicFrame frame(QuicPathResponseFrame(kInvalidControlFrameId, data_buffer));
  if (AddPaddedFrameWithRetry(frame)) {
    return true;
  }

  QUIC_DVLOG(1) << ENDPOINT << "Can't send PATH_RESPONSE now";
  return false;
}

bool QuicPacketCreator::AddPaddedFrameWithRetry(const QuicFrame& frame) {
  if (HasPendingFrames()) {
    if (AddPaddedSavedFrame(frame, NOT_RETRANSMISSION)) {
      // Frame is queued.
      return true;
    }
  }
  // Frame was not queued but queued frames were flushed.
  QUICHE_DCHECK(!HasPendingFrames()) << ENDPOINT;
  if (!delegate_->ShouldGeneratePacket(NO_RETRANSMITTABLE_DATA,
                                       NOT_HANDSHAKE)) {
    return false;
  }
  bool success = AddPaddedSavedFrame(frame, NOT_RETRANSMISSION);
  QUIC_BUG_IF(quic_bug_12398_20, !success) << ENDPOINT;
  return true;
}

bool QuicPacketCreator::HasRetryToken() const { return !retry_token_.empty(); }

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
