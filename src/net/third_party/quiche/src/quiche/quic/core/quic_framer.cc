// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_framer.h"

#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/crypto_framer.h"
#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/crypto_handshake_message.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/crypto/null_decrypter.h"
#include "quiche/quic/core/crypto/quic_decrypter.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/frames/quic_ack_frequency_frame.h"
#include "quiche/quic/core/frames/quic_immediate_ack_frame.h"
#include "quiche/quic/core/frames/quic_reset_stream_at_frame.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_data_reader.h"
#include "quiche/quic/core/quic_data_writer.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_socket_address_coder.h"
#include "quiche/quic/core/quic_stream_frame_data_producer.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_client_stats.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/common/wire_serialization.h"

namespace quic {

namespace {

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

// There are two interpretations for the Frame Type byte in the QUIC protocol,
// resulting in two Frame Types: Special Frame Types and Regular Frame Types.
//
// Regular Frame Types use the Frame Type byte simply. Currently defined
// Regular Frame Types are:
// Padding            : 0b 00000000 (0x00)
// ResetStream        : 0b 00000001 (0x01)
// ConnectionClose    : 0b 00000010 (0x02)
// GoAway             : 0b 00000011 (0x03)
// WindowUpdate       : 0b 00000100 (0x04)
// Blocked            : 0b 00000101 (0x05)
//
// Special Frame Types encode both a Frame Type and corresponding flags
// all in the Frame Type byte. Currently defined Special Frame Types
// are:
// Stream             : 0b 1xxxxxxx
// Ack                : 0b 01xxxxxx
//
// Semantics of the flag bits above (the x bits) depends on the frame type.

// Masks to determine if the frame type is a special use
// and for specific special frame types.
const uint8_t kQuicFrameTypeSpecialMask = 0xC0;  // 0b 11000000
const uint8_t kQuicFrameTypeStreamMask = 0x80;
const uint8_t kQuicFrameTypeAckMask = 0x40;
static_assert(kQuicFrameTypeSpecialMask ==
                  (kQuicFrameTypeStreamMask | kQuicFrameTypeAckMask),
              "Invalid kQuicFrameTypeSpecialMask");

// The stream type format is 1FDOOOSS, where
//    F is the fin bit.
//    D is the data length bit (0 or 2 bytes).
//    OO/OOO are the size of the offset.
//    SS is the size of the stream ID.
// Note that the stream encoding can not be determined by inspection. It can
// be determined only by knowing the QUIC Version.
// Stream frame relative shifts and masks for interpreting the stream flags.
// StreamID may be 1, 2, 3, or 4 bytes.
const uint8_t kQuicStreamIdShift = 2;
const uint8_t kQuicStreamIDLengthMask = 0x03;

// Offset may be 0, 2, 4, or 8 bytes.
const uint8_t kQuicStreamShift = 3;
const uint8_t kQuicStreamOffsetMask = 0x07;

// Data length may be 0 or 2 bytes.
const uint8_t kQuicStreamDataLengthShift = 1;
const uint8_t kQuicStreamDataLengthMask = 0x01;

// Fin bit may be set or not.
const uint8_t kQuicStreamFinShift = 1;
const uint8_t kQuicStreamFinMask = 0x01;

// The format is 01M0LLOO, where
//   M if set, there are multiple ack blocks in the frame.
//  LL is the size of the largest ack field.
//  OO is the size of the ack blocks offset field.
// packet number size shift used in AckFrames.
const uint8_t kQuicSequenceNumberLengthNumBits = 2;
const uint8_t kActBlockLengthOffset = 0;
const uint8_t kLargestAckedOffset = 2;

// Acks may have only one ack block.
const uint8_t kQuicHasMultipleAckBlocksOffset = 5;

// Timestamps are 4 bytes followed by 2 bytes.
const uint8_t kQuicNumTimestampsLength = 1;
const uint8_t kQuicFirstTimestampLength = 4;
const uint8_t kQuicTimestampLength = 2;
// Gaps between packet numbers are 1 byte.
const uint8_t kQuicTimestampPacketNumberGapLength = 1;

// Maximum length of encoded error strings.
const int kMaxErrorStringLength = 256;

const uint8_t kConnectionIdLengthAdjustment = 3;
const uint8_t kDestinationConnectionIdLengthMask = 0xF0;
const uint8_t kSourceConnectionIdLengthMask = 0x0F;

// Returns the absolute value of the difference between |a| and |b|.
uint64_t Delta(uint64_t a, uint64_t b) {
  // Since these are unsigned numbers, we can't just return abs(a - b)
  if (a < b) {
    return b - a;
  }
  return a - b;
}

uint64_t ClosestTo(uint64_t target, uint64_t a, uint64_t b) {
  return (Delta(target, a) < Delta(target, b)) ? a : b;
}

QuicPacketNumberLength ReadAckPacketNumberLength(uint8_t flags) {
  switch (flags & PACKET_FLAGS_8BYTE_PACKET) {
    case PACKET_FLAGS_8BYTE_PACKET:
      return PACKET_6BYTE_PACKET_NUMBER;
    case PACKET_FLAGS_4BYTE_PACKET:
      return PACKET_4BYTE_PACKET_NUMBER;
    case PACKET_FLAGS_2BYTE_PACKET:
      return PACKET_2BYTE_PACKET_NUMBER;
    case PACKET_FLAGS_1BYTE_PACKET:
      return PACKET_1BYTE_PACKET_NUMBER;
    default:
      QUIC_BUG(quic_bug_10850_2) << "Unreachable case statement.";
      return PACKET_6BYTE_PACKET_NUMBER;
  }
}

uint8_t PacketNumberLengthToOnWireValue(
    QuicPacketNumberLength packet_number_length) {
  return packet_number_length - 1;
}

QuicPacketNumberLength GetShortHeaderPacketNumberLength(uint8_t type) {
  QUICHE_DCHECK(!(type & FLAGS_LONG_HEADER));
  return static_cast<QuicPacketNumberLength>((type & 0x03) + 1);
}

uint8_t LongHeaderTypeToOnWireValue(QuicLongHeaderType type,
                                    const ParsedQuicVersion& version) {
  switch (type) {
    case INITIAL:
      return version.UsesV2PacketTypes() ? (1 << 4) : 0;
    case ZERO_RTT_PROTECTED:
      return version.UsesV2PacketTypes() ? (2 << 4) : (1 << 4);
    case HANDSHAKE:
      return version.UsesV2PacketTypes() ? (3 << 4) : (2 << 4);
    case RETRY:
      return version.UsesV2PacketTypes() ? 0 : (3 << 4);
    case VERSION_NEGOTIATION:
      return 0xF0;  // Value does not matter
    default:
      QUIC_BUG(quic_bug_10850_3) << "Invalid long header type: " << type;
      return 0xFF;
  }
}

QuicLongHeaderType GetLongHeaderType(uint8_t type,
                                     const ParsedQuicVersion& version) {
  QUICHE_DCHECK((type & FLAGS_LONG_HEADER));
  switch ((type & 0x30) >> 4) {
    case 0:
      return version.UsesV2PacketTypes() ? RETRY : INITIAL;
    case 1:
      return version.UsesV2PacketTypes() ? INITIAL : ZERO_RTT_PROTECTED;
    case 2:
      return version.UsesV2PacketTypes() ? ZERO_RTT_PROTECTED : HANDSHAKE;
    case 3:
      return version.UsesV2PacketTypes() ? HANDSHAKE : RETRY;
    default:
      QUIC_BUG(quic_bug_10850_4) << "Unreachable statement";
      return INVALID_PACKET_TYPE;
  }
}

QuicPacketNumberLength GetLongHeaderPacketNumberLength(uint8_t type) {
  return static_cast<QuicPacketNumberLength>((type & 0x03) + 1);
}

// Used to get packet number space before packet gets decrypted.
PacketNumberSpace GetPacketNumberSpace(const QuicPacketHeader& header) {
  switch (header.form) {
    case GOOGLE_QUIC_PACKET:
      QUIC_BUG(quic_bug_10850_5)
          << "Try to get packet number space of Google QUIC packet";
      break;
    case IETF_QUIC_SHORT_HEADER_PACKET:
      return APPLICATION_DATA;
    case IETF_QUIC_LONG_HEADER_PACKET:
      switch (header.long_packet_type) {
        case INITIAL:
          return INITIAL_DATA;
        case HANDSHAKE:
          return HANDSHAKE_DATA;
        case ZERO_RTT_PROTECTED:
          return APPLICATION_DATA;
        case VERSION_NEGOTIATION:
        case RETRY:
        case INVALID_PACKET_TYPE:
          QUIC_BUG(quic_bug_10850_6)
              << "Try to get packet number space of long header type: "
              << QuicUtils::QuicLongHeaderTypetoString(header.long_packet_type);
          break;
      }
  }

  return NUM_PACKET_NUMBER_SPACES;
}

EncryptionLevel GetEncryptionLevel(const QuicPacketHeader& header) {
  switch (header.form) {
    case GOOGLE_QUIC_PACKET:
      QUIC_BUG(quic_bug_10850_7)
          << "Cannot determine EncryptionLevel from Google QUIC header";
      break;
    case IETF_QUIC_SHORT_HEADER_PACKET:
      return ENCRYPTION_FORWARD_SECURE;
    case IETF_QUIC_LONG_HEADER_PACKET:
      switch (header.long_packet_type) {
        case INITIAL:
          return ENCRYPTION_INITIAL;
        case HANDSHAKE:
          return ENCRYPTION_HANDSHAKE;
        case ZERO_RTT_PROTECTED:
          return ENCRYPTION_ZERO_RTT;
        case VERSION_NEGOTIATION:
        case RETRY:
        case INVALID_PACKET_TYPE:
          QUIC_BUG(quic_bug_10850_8)
              << "No encryption used with type "
              << QuicUtils::QuicLongHeaderTypetoString(header.long_packet_type);
      }
  }
  return NUM_ENCRYPTION_LEVELS;
}

absl::string_view TruncateErrorString(absl::string_view error) {
  if (error.length() <= kMaxErrorStringLength) {
    return error;
  }
  return absl::string_view(error.data(), kMaxErrorStringLength);
}

size_t TruncatedErrorStringSize(const absl::string_view& error) {
  if (error.length() < kMaxErrorStringLength) {
    return error.length();
  }
  return kMaxErrorStringLength;
}

uint8_t GetConnectionIdLengthValue(uint8_t length) {
  if (length == 0) {
    return 0;
  }
  return static_cast<uint8_t>(length - kConnectionIdLengthAdjustment);
}

bool IsValidPacketNumberLength(QuicPacketNumberLength packet_number_length) {
  size_t length = packet_number_length;
  return length == 1 || length == 2 || length == 4 || length == 6 ||
         length == 8;
}

bool IsValidFullPacketNumber(uint64_t full_packet_number,
                             ParsedQuicVersion version) {
  return full_packet_number > 0 || version.HasIetfQuicFrames();
}

bool AppendIetfConnectionIds(bool version_flag, bool use_length_prefix,
                             const QuicConnectionId& destination_connection_id,
                             const QuicConnectionId& source_connection_id,
                             QuicDataWriter* writer) {
  if (!version_flag) {
    return writer->WriteConnectionId(destination_connection_id);
  }

  if (use_length_prefix) {
    return writer->WriteLengthPrefixedConnectionId(destination_connection_id) &&
           writer->WriteLengthPrefixedConnectionId(source_connection_id);
  }

  // Compute connection ID length byte.
  uint8_t dcil = GetConnectionIdLengthValue(destination_connection_id.length());
  uint8_t scil = GetConnectionIdLengthValue(source_connection_id.length());
  uint8_t connection_id_length = dcil << 4 | scil;

  return writer->WriteUInt8(connection_id_length) &&
         writer->WriteConnectionId(destination_connection_id) &&
         writer->WriteConnectionId(source_connection_id);
}

enum class DroppedPacketReason {
  // General errors
  INVALID_PUBLIC_HEADER,
  VERSION_MISMATCH,
  // Version negotiation packet errors
  INVALID_VERSION_NEGOTIATION_PACKET,
  // Public reset packet errors, pre-v44
  INVALID_PUBLIC_RESET_PACKET,
  // Data packet errors
  INVALID_PACKET_NUMBER,
  INVALID_DIVERSIFICATION_NONCE,
  DECRYPTION_FAILURE,
  NUM_REASONS,
};

void RecordDroppedPacketReason(DroppedPacketReason reason) {
  QUIC_CLIENT_HISTOGRAM_ENUM("QuicDroppedPacketReason", reason,
                             DroppedPacketReason::NUM_REASONS,
                             "The reason a packet was not processed. Recorded "
                             "each time such a packet is dropped");
}

PacketHeaderFormat GetIetfPacketHeaderFormat(uint8_t type_byte) {
  return type_byte & FLAGS_LONG_HEADER ? IETF_QUIC_LONG_HEADER_PACKET
                                       : IETF_QUIC_SHORT_HEADER_PACKET;
}

std::string GenerateErrorString(std::string initial_error_string,
                                QuicErrorCode quic_error_code) {
  if (quic_error_code == QUIC_IETF_GQUIC_ERROR_MISSING) {
    // QUIC_IETF_GQUIC_ERROR_MISSING is special -- it means not to encode
    // the error value in the string.
    return initial_error_string;
  }
  return absl::StrCat(std::to_string(static_cast<unsigned>(quic_error_code)),
                      ":", initial_error_string);
}

// Return the minimum size of the ECN fields in an ACK frame
size_t AckEcnCountSize(const QuicAckFrame& ack_frame) {
  if (!ack_frame.ecn_counters.has_value()) {
    return 0;
  }
  return (QuicDataWriter::GetVarInt62Len(ack_frame.ecn_counters->ect0) +
          QuicDataWriter::GetVarInt62Len(ack_frame.ecn_counters->ect1) +
          QuicDataWriter::GetVarInt62Len(ack_frame.ecn_counters->ce));
}

}  // namespace

QuicFramer::QuicFramer(const ParsedQuicVersionVector& supported_versions,
                       QuicTime creation_time, Perspective perspective,
                       uint8_t expected_server_connection_id_length)
    : visitor_(nullptr),
      error_(QUIC_NO_ERROR),
      last_serialized_server_connection_id_(EmptyQuicConnectionId()),
      version_(ParsedQuicVersion::Unsupported()),
      supported_versions_(supported_versions),
      decrypter_level_(ENCRYPTION_INITIAL),
      alternative_decrypter_level_(NUM_ENCRYPTION_LEVELS),
      alternative_decrypter_latch_(false),
      perspective_(perspective),
      validate_flags_(true),
      process_timestamps_(false),
      max_receive_timestamps_per_ack_(std::numeric_limits<uint32_t>::max()),
      receive_timestamps_exponent_(0),
      process_reset_stream_at_(false),
      creation_time_(creation_time),
      last_timestamp_(QuicTime::Delta::Zero()),
      support_key_update_for_connection_(false),
      current_key_phase_bit_(false),
      potential_peer_key_update_attempt_count_(0),
      first_sending_packet_number_(FirstSendingPacketNumber()),
      data_producer_(nullptr),
      expected_server_connection_id_length_(
          expected_server_connection_id_length),
      expected_client_connection_id_length_(0),
      supports_multiple_packet_number_spaces_(false),
      last_written_packet_number_length_(0),
      peer_ack_delay_exponent_(kDefaultAckDelayExponent),
      local_ack_delay_exponent_(kDefaultAckDelayExponent),
      current_received_frame_type_(0),
      previously_received_frame_type_(0) {
  QUICHE_DCHECK(!supported_versions.empty());
  version_ = supported_versions_[0];
  QUICHE_DCHECK(version_.IsKnown())
      << ParsedQuicVersionVectorToString(supported_versions_);
}

QuicFramer::~QuicFramer() {}

// static
size_t QuicFramer::GetMinStreamFrameSize(QuicTransportVersion version,
                                         QuicStreamId stream_id,
                                         QuicStreamOffset offset,
                                         bool last_frame_in_packet,
                                         size_t data_length) {
  if (VersionHasIetfQuicFrames(version)) {
    return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(stream_id) +
           (last_frame_in_packet
                ? 0
                : QuicDataWriter::GetVarInt62Len(data_length)) +
           (offset != 0 ? QuicDataWriter::GetVarInt62Len(offset) : 0);
  }
  return kQuicFrameTypeSize + GetStreamIdSize(stream_id) +
         GetStreamOffsetSize(offset) +
         (last_frame_in_packet ? 0 : kQuicStreamPayloadLengthSize);
}

// static
size_t QuicFramer::GetMinCryptoFrameSize(QuicStreamOffset offset,
                                         QuicPacketLength data_length) {
  return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(offset) +
         QuicDataWriter::GetVarInt62Len(data_length);
}

// static
size_t QuicFramer::GetMessageFrameSize(bool last_frame_in_packet,
                                       QuicByteCount length) {
  return kQuicFrameTypeSize +
         (last_frame_in_packet ? 0 : QuicDataWriter::GetVarInt62Len(length)) +
         length;
}

// static
size_t QuicFramer::GetMinAckFrameSize(
    QuicTransportVersion version, const QuicAckFrame& ack_frame,
    uint32_t local_ack_delay_exponent,
    bool use_ietf_ack_with_receive_timestamp) {
  if (VersionHasIetfQuicFrames(version)) {
    // The minimal ack frame consists of the following fields: Largest
    // Acknowledged, ACK Delay, 0 ACK Block Count, First ACK Block and either 0
    // Timestamp Range Count or ECN counts.
    // Type byte + largest acked.
    size_t min_size =
        kQuicFrameTypeSize +
        QuicDataWriter::GetVarInt62Len(LargestAcked(ack_frame).ToUint64());
    // Ack delay.
    min_size += QuicDataWriter::GetVarInt62Len(
        ack_frame.ack_delay_time.ToMicroseconds() >> local_ack_delay_exponent);
    // 0 ack block count.
    min_size += QuicDataWriter::GetVarInt62Len(0);
    // First ack block.
    min_size += QuicDataWriter::GetVarInt62Len(
        ack_frame.packets.Empty() ? 0
                                  : ack_frame.packets.rbegin()->Length() - 1);

    if (use_ietf_ack_with_receive_timestamp) {
      // 0 Timestamp Range Count.
      min_size += QuicDataWriter::GetVarInt62Len(0);
    } else {
      min_size += AckEcnCountSize(ack_frame);
    }
    return min_size;
  }
  return kQuicFrameTypeSize +
         GetMinPacketNumberLength(LargestAcked(ack_frame)) +
         kQuicDeltaTimeLargestObservedSize + kQuicNumTimestampsSize;
}

// static
size_t QuicFramer::GetStopWaitingFrameSize(
    QuicPacketNumberLength packet_number_length) {
  size_t min_size = kQuicFrameTypeSize + packet_number_length;
  return min_size;
}

// static
size_t QuicFramer::GetRstStreamFrameSize(QuicTransportVersion version,
                                         const QuicRstStreamFrame& frame) {
  if (VersionHasIetfQuicFrames(version)) {
    return QuicDataWriter::GetVarInt62Len(frame.stream_id) +
           QuicDataWriter::GetVarInt62Len(frame.byte_offset) +
           kQuicFrameTypeSize +
           QuicDataWriter::GetVarInt62Len(frame.ietf_error_code);
  }
  return kQuicFrameTypeSize + kQuicMaxStreamIdSize + kQuicMaxStreamOffsetSize +
         kQuicErrorCodeSize;
}

// static
size_t QuicFramer::GetConnectionCloseFrameSize(
    QuicTransportVersion version, const QuicConnectionCloseFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    // Not IETF QUIC, return Google QUIC CONNECTION CLOSE frame size.
    return kQuicFrameTypeSize + kQuicErrorCodeSize +
           kQuicErrorDetailsLengthSize +
           TruncatedErrorStringSize(frame.error_details);
  }

  // Prepend the extra error information to the string and get the result's
  // length.
  const size_t truncated_error_string_size = TruncatedErrorStringSize(
      GenerateErrorString(frame.error_details, frame.quic_error_code));

  const size_t frame_size =
      truncated_error_string_size +
      QuicDataWriter::GetVarInt62Len(truncated_error_string_size) +
      kQuicFrameTypeSize +
      QuicDataWriter::GetVarInt62Len(frame.wire_error_code);
  if (frame.close_type == IETF_QUIC_APPLICATION_CONNECTION_CLOSE) {
    return frame_size;
  }
  // The Transport close frame has the transport_close_frame_type, so include
  // its length.
  return frame_size +
         QuicDataWriter::GetVarInt62Len(frame.transport_close_frame_type);
}

// static
size_t QuicFramer::GetMinGoAwayFrameSize() {
  return kQuicFrameTypeSize + kQuicErrorCodeSize + kQuicErrorDetailsLengthSize +
         kQuicMaxStreamIdSize;
}

// static
size_t QuicFramer::GetWindowUpdateFrameSize(
    QuicTransportVersion version, const QuicWindowUpdateFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    return kQuicFrameTypeSize + kQuicMaxStreamIdSize + kQuicMaxStreamOffsetSize;
  }
  if (frame.stream_id == QuicUtils::GetInvalidStreamId(version)) {
    // Frame would be a MAX DATA frame, which has only a Maximum Data field.
    return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(frame.max_data);
  }
  // Frame would be MAX STREAM DATA, has Maximum Stream Data and Stream ID
  // fields.
  return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(frame.max_data) +
         QuicDataWriter::GetVarInt62Len(frame.stream_id);
}

// static
size_t QuicFramer::GetMaxStreamsFrameSize(QuicTransportVersion version,
                                          const QuicMaxStreamsFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    QUIC_BUG(quic_bug_10850_9)
        << "In version " << version
        << ", which does not support IETF Frames, and tried to serialize "
           "MaxStreams Frame.";
  }
  return kQuicFrameTypeSize +
         QuicDataWriter::GetVarInt62Len(frame.stream_count);
}

// static
size_t QuicFramer::GetStreamsBlockedFrameSize(
    QuicTransportVersion version, const QuicStreamsBlockedFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    QUIC_BUG(quic_bug_10850_10)
        << "In version " << version
        << ", which does not support IETF frames, and tried to serialize "
           "StreamsBlocked Frame.";
  }

  return kQuicFrameTypeSize +
         QuicDataWriter::GetVarInt62Len(frame.stream_count);
}

// static
size_t QuicFramer::GetBlockedFrameSize(QuicTransportVersion version,
                                       const QuicBlockedFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    return kQuicFrameTypeSize + kQuicMaxStreamIdSize;
  }
  if (frame.stream_id == QuicUtils::GetInvalidStreamId(version)) {
    // return size of IETF QUIC Blocked frame
    return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(frame.offset);
  }
  // return size of IETF QUIC Stream Blocked frame.
  return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(frame.offset) +
         QuicDataWriter::GetVarInt62Len(frame.stream_id);
}

// static
size_t QuicFramer::GetStopSendingFrameSize(const QuicStopSendingFrame& frame) {
  return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(frame.stream_id) +
         QuicDataWriter::GetVarInt62Len(frame.ietf_error_code);
}

// static
size_t QuicFramer::GetAckFrequencyFrameSize(
    const QuicAckFrequencyFrame& frame) {
  return QuicDataWriter::GetVarInt62Len(IETF_ACK_FREQUENCY) +
         QuicDataWriter::GetVarInt62Len(frame.sequence_number) +
         QuicDataWriter::GetVarInt62Len(frame.packet_tolerance) +
         QuicDataWriter::GetVarInt62Len(frame.max_ack_delay.ToMicroseconds()) +
         // One byte for encoding boolean
         1;
}

// static
size_t QuicFramer::GetResetStreamAtFrameSize(
    const QuicResetStreamAtFrame& frame) {
  return QuicDataWriter::GetVarInt62Len(IETF_RESET_STREAM_AT) +
         QuicDataWriter::GetVarInt62Len(frame.stream_id) +
         QuicDataWriter::GetVarInt62Len(frame.error) +
         QuicDataWriter::GetVarInt62Len(frame.final_offset) +
         QuicDataWriter::GetVarInt62Len(frame.reliable_offset);
}

// static
size_t QuicFramer::GetPathChallengeFrameSize(
    const QuicPathChallengeFrame& frame) {
  return kQuicFrameTypeSize + sizeof(frame.data_buffer);
}

// static
size_t QuicFramer::GetPathResponseFrameSize(
    const QuicPathResponseFrame& frame) {
  return kQuicFrameTypeSize + sizeof(frame.data_buffer);
}

// static
size_t QuicFramer::GetRetransmittableControlFrameSize(
    QuicTransportVersion version, const QuicFrame& frame) {
  switch (frame.type) {
    case PING_FRAME:
      // Ping has no payload.
      return kQuicFrameTypeSize;
    case RST_STREAM_FRAME:
      return GetRstStreamFrameSize(version, *frame.rst_stream_frame);
    case CONNECTION_CLOSE_FRAME:
      return GetConnectionCloseFrameSize(version,
                                         *frame.connection_close_frame);
    case GOAWAY_FRAME:
      return GetMinGoAwayFrameSize() +
             TruncatedErrorStringSize(frame.goaway_frame->reason_phrase);
    case WINDOW_UPDATE_FRAME:
      // For IETF QUIC, this could be either a MAX DATA or MAX STREAM DATA.
      // GetWindowUpdateFrameSize figures this out and returns the correct
      // length.
      return GetWindowUpdateFrameSize(version, frame.window_update_frame);
    case BLOCKED_FRAME:
      return GetBlockedFrameSize(version, frame.blocked_frame);
    case NEW_CONNECTION_ID_FRAME:
      return GetNewConnectionIdFrameSize(*frame.new_connection_id_frame);
    case RETIRE_CONNECTION_ID_FRAME:
      return GetRetireConnectionIdFrameSize(*frame.retire_connection_id_frame);
    case NEW_TOKEN_FRAME:
      return GetNewTokenFrameSize(*frame.new_token_frame);
    case MAX_STREAMS_FRAME:
      return GetMaxStreamsFrameSize(version, frame.max_streams_frame);
    case STREAMS_BLOCKED_FRAME:
      return GetStreamsBlockedFrameSize(version, frame.streams_blocked_frame);
    case PATH_RESPONSE_FRAME:
      return GetPathResponseFrameSize(frame.path_response_frame);
    case PATH_CHALLENGE_FRAME:
      return GetPathChallengeFrameSize(frame.path_challenge_frame);
    case STOP_SENDING_FRAME:
      return GetStopSendingFrameSize(frame.stop_sending_frame);
    case HANDSHAKE_DONE_FRAME:
      // HANDSHAKE_DONE has no payload.
      return kQuicFrameTypeSize;
    case ACK_FREQUENCY_FRAME:
      return GetAckFrequencyFrameSize(*frame.ack_frequency_frame);
    case IMMEDIATE_ACK_FRAME:
      // IMMEDIATE_ACK has no payload.
      return QuicDataWriter::GetVarInt62Len(IETF_IMMEDIATE_ACK);
    case RESET_STREAM_AT_FRAME:
      return GetResetStreamAtFrameSize(*frame.reset_stream_at_frame);
    case STREAM_FRAME:
    case ACK_FRAME:
    case STOP_WAITING_FRAME:
    case MTU_DISCOVERY_FRAME:
    case PADDING_FRAME:
    case MESSAGE_FRAME:
    case CRYPTO_FRAME:
    case NUM_FRAME_TYPES:
      QUICHE_DCHECK(false);
      return 0;
  }

  // Not reachable, but some Chrome compilers can't figure that out.  *sigh*
  QUICHE_DCHECK(false);
  return 0;
}

// static
size_t QuicFramer::GetStreamIdSize(QuicStreamId stream_id) {
  // Sizes are 1 through 4 bytes.
  for (int i = 1; i <= 4; ++i) {
    stream_id >>= 8;
    if (stream_id == 0) {
      return i;
    }
  }
  QUIC_BUG(quic_bug_10850_11) << "Failed to determine StreamIDSize.";
  return 4;
}

// static
size_t QuicFramer::GetStreamOffsetSize(QuicStreamOffset offset) {
  // 0 is a special case.
  if (offset == 0) {
    return 0;
  }
  // 2 through 8 are the remaining sizes.
  offset >>= 8;
  for (int i = 2; i <= 8; ++i) {
    offset >>= 8;
    if (offset == 0) {
      return i;
    }
  }
  QUIC_BUG(quic_bug_10850_12) << "Failed to determine StreamOffsetSize.";
  return 8;
}

// static
size_t QuicFramer::GetNewConnectionIdFrameSize(
    const QuicNewConnectionIdFrame& frame) {
  return kQuicFrameTypeSize +
         QuicDataWriter::GetVarInt62Len(frame.sequence_number) +
         QuicDataWriter::GetVarInt62Len(frame.retire_prior_to) +
         kConnectionIdLengthSize + frame.connection_id.length() +
         sizeof(frame.stateless_reset_token);
}

// static
size_t QuicFramer::GetRetireConnectionIdFrameSize(
    const QuicRetireConnectionIdFrame& frame) {
  return kQuicFrameTypeSize +
         QuicDataWriter::GetVarInt62Len(frame.sequence_number);
}

// static
size_t QuicFramer::GetNewTokenFrameSize(const QuicNewTokenFrame& frame) {
  return kQuicFrameTypeSize +
         QuicDataWriter::GetVarInt62Len(frame.token.length()) +
         frame.token.length();
}

bool QuicFramer::IsSupportedVersion(const ParsedQuicVersion version) const {
  for (const ParsedQuicVersion& supported_version : supported_versions_) {
    if (version == supported_version) {
      return true;
    }
  }
  return false;
}

size_t QuicFramer::GetSerializedFrameLength(
    const QuicFrame& frame, size_t free_bytes, bool first_frame,
    bool last_frame, QuicPacketNumberLength packet_number_length) {
  // Prevent a rare crash reported in b/19458523.
  if (frame.type == ACK_FRAME && frame.ack_frame == nullptr) {
    QUIC_BUG(quic_bug_10850_13)
        << "Cannot compute the length of a null ack frame. free_bytes:"
        << free_bytes << " first_frame:" << first_frame
        << " last_frame:" << last_frame
        << " seq num length:" << packet_number_length;
    set_error(QUIC_INTERNAL_ERROR);
    visitor_->OnError(this);
    return 0;
  }
  if (frame.type == PADDING_FRAME) {
    if (frame.padding_frame.num_padding_bytes == -1) {
      // Full padding to the end of the packet.
      return free_bytes;
    } else {
      // Lite padding.
      return free_bytes <
                     static_cast<size_t>(frame.padding_frame.num_padding_bytes)
                 ? free_bytes
                 : frame.padding_frame.num_padding_bytes;
    }
  }

  size_t frame_len =
      ComputeFrameLength(frame, last_frame, packet_number_length);
  if (frame_len <= free_bytes) {
    // Frame fits within packet. Note that acks may be truncated.
    return frame_len;
  }
  // Only truncate the first frame in a packet, so if subsequent ones go
  // over, stop including more frames.
  if (!first_frame) {
    return 0;
  }
  bool can_truncate =
      frame.type == ACK_FRAME &&
      free_bytes >=
          GetMinAckFrameSize(version_.transport_version, *frame.ack_frame,
                             local_ack_delay_exponent_,
                             UseIetfAckWithReceiveTimestamp(*frame.ack_frame));
  if (can_truncate) {
    // Truncate the frame so the packet will not exceed kMaxOutgoingPacketSize.
    // Note that we may not use every byte of the writer in this case.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Truncating large frame, free bytes: " << free_bytes;
    return free_bytes;
  }
  return 0;
}

QuicFramer::AckFrameInfo::AckFrameInfo()
    : max_block_length(0), first_block_length(0), num_ack_blocks(0) {}

QuicFramer::AckFrameInfo::AckFrameInfo(const AckFrameInfo& other) = default;

QuicFramer::AckFrameInfo::~AckFrameInfo() {}

bool QuicFramer::WriteIetfLongHeaderLength(const QuicPacketHeader& header,
                                           QuicDataWriter* writer,
                                           size_t length_field_offset,
                                           EncryptionLevel level) {
  if (!QuicVersionHasLongHeaderLengths(transport_version()) ||
      !header.version_flag || length_field_offset == 0) {
    return true;
  }
  if (writer->length() < length_field_offset ||
      writer->length() - length_field_offset <
          quiche::kQuicheDefaultLongHeaderLengthLength) {
    set_detailed_error("Invalid length_field_offset.");
    QUIC_BUG(quic_bug_10850_14) << "Invalid length_field_offset.";
    return false;
  }
  size_t length_to_write = writer->length() - length_field_offset -
                           quiche::kQuicheDefaultLongHeaderLengthLength;
  // Add length of auth tag.
  length_to_write = GetCiphertextSize(level, length_to_write);

  QuicDataWriter length_writer(writer->length() - length_field_offset,
                               writer->data() + length_field_offset);
  if (!length_writer.WriteVarInt62WithForcedLength(
          length_to_write, quiche::kQuicheDefaultLongHeaderLengthLength)) {
    set_detailed_error("Failed to overwrite long header length.");
    QUIC_BUG(quic_bug_10850_15) << "Failed to overwrite long header length.";
    return false;
  }
  return true;
}

size_t QuicFramer::BuildDataPacket(const QuicPacketHeader& header,
                                   const QuicFrames& frames, char* buffer,
                                   size_t packet_length,
                                   EncryptionLevel level) {
  QUIC_BUG_IF(quic_bug_12975_2, header.version_flag &&
                                    header.long_packet_type == RETRY &&
                                    !frames.empty())
      << "IETF RETRY packets cannot contain frames " << header;
  QuicDataWriter writer(packet_length, buffer);
  size_t length_field_offset = 0;
  if (!AppendIetfPacketHeader(header, &writer, &length_field_offset)) {
    QUIC_BUG(quic_bug_10850_16) << "AppendPacketHeader failed";
    return 0;
  }

  if (VersionHasIetfQuicFrames(transport_version())) {
    if (AppendIetfFrames(frames, &writer) == 0) {
      return 0;
    }
    if (!WriteIetfLongHeaderLength(header, &writer, length_field_offset,
                                   level)) {
      return 0;
    }
    return writer.length();
  }

  size_t i = 0;
  for (const QuicFrame& frame : frames) {
    // Determine if we should write stream frame length in header.
    const bool last_frame_in_packet = i == frames.size() - 1;
    if (!AppendTypeByte(frame, last_frame_in_packet, &writer)) {
      QUIC_BUG(quic_bug_10850_17) << "AppendTypeByte failed";
      return 0;
    }

    switch (frame.type) {
      case PADDING_FRAME:
        if (!AppendPaddingFrame(frame.padding_frame, &writer)) {
          QUIC_BUG(quic_bug_10850_18)
              << "AppendPaddingFrame of "
              << frame.padding_frame.num_padding_bytes << " failed";
          return 0;
        }
        break;
      case STREAM_FRAME:
        if (!AppendStreamFrame(frame.stream_frame, last_frame_in_packet,
                               &writer)) {
          QUIC_BUG(quic_bug_10850_19) << "AppendStreamFrame failed";
          return 0;
        }
        break;
      case ACK_FRAME:
        if (!AppendAckFrameAndTypeByte(*frame.ack_frame, &writer)) {
          QUIC_BUG(quic_bug_10850_20)
              << "AppendAckFrameAndTypeByte failed: " << detailed_error_;
          return 0;
        }
        break;
      case MTU_DISCOVERY_FRAME:
        // MTU discovery frames are serialized as ping frames.
        ABSL_FALLTHROUGH_INTENDED;
      case PING_FRAME:
        // Ping has no payload.
        break;
      case RST_STREAM_FRAME:
        if (!AppendRstStreamFrame(*frame.rst_stream_frame, &writer)) {
          QUIC_BUG(quic_bug_10850_22) << "AppendRstStreamFrame failed";
          return 0;
        }
        break;
      case CONNECTION_CLOSE_FRAME:
        if (!AppendConnectionCloseFrame(*frame.connection_close_frame,
                                        &writer)) {
          QUIC_BUG(quic_bug_10850_23) << "AppendConnectionCloseFrame failed";
          return 0;
        }
        break;
      case GOAWAY_FRAME:
        if (!AppendGoAwayFrame(*frame.goaway_frame, &writer)) {
          QUIC_BUG(quic_bug_10850_24) << "AppendGoAwayFrame failed";
          return 0;
        }
        break;
      case WINDOW_UPDATE_FRAME:
        if (!AppendWindowUpdateFrame(frame.window_update_frame, &writer)) {
          QUIC_BUG(quic_bug_10850_25) << "AppendWindowUpdateFrame failed";
          return 0;
        }
        break;
      case BLOCKED_FRAME:
        if (!AppendBlockedFrame(frame.blocked_frame, &writer)) {
          QUIC_BUG(quic_bug_10850_26) << "AppendBlockedFrame failed";
          return 0;
        }
        break;
      case NEW_CONNECTION_ID_FRAME:
        set_detailed_error(
            "Attempt to append NEW_CONNECTION_ID frame and not in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case RETIRE_CONNECTION_ID_FRAME:
        set_detailed_error(
            "Attempt to append RETIRE_CONNECTION_ID frame and not in IETF "
            "QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case NEW_TOKEN_FRAME:
        set_detailed_error(
            "Attempt to append NEW_TOKEN_ID frame and not in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case MAX_STREAMS_FRAME:
        set_detailed_error(
            "Attempt to append MAX_STREAMS frame and not in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case STREAMS_BLOCKED_FRAME:
        set_detailed_error(
            "Attempt to append STREAMS_BLOCKED frame and not in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case PATH_RESPONSE_FRAME:
        set_detailed_error(
            "Attempt to append PATH_RESPONSE frame and not in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case PATH_CHALLENGE_FRAME:
        set_detailed_error(
            "Attempt to append PATH_CHALLENGE frame and not in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case STOP_SENDING_FRAME:
        set_detailed_error(
            "Attempt to append STOP_SENDING frame and not in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case MESSAGE_FRAME:
        if (!AppendMessageFrameAndTypeByte(*frame.message_frame,
                                           last_frame_in_packet, &writer)) {
          QUIC_BUG(quic_bug_10850_27) << "AppendMessageFrame failed";
          return 0;
        }
        break;
      case CRYPTO_FRAME:
        if (!QuicVersionUsesCryptoFrames(version_.transport_version)) {
          set_detailed_error(
              "Attempt to append CRYPTO frame in version prior to 47.");
          return RaiseError(QUIC_INTERNAL_ERROR);
        }
        if (!AppendCryptoFrame(*frame.crypto_frame, &writer)) {
          QUIC_BUG(quic_bug_10850_28) << "AppendCryptoFrame failed";
          return 0;
        }
        break;
      case HANDSHAKE_DONE_FRAME:
        // HANDSHAKE_DONE has no payload.
        break;
      default:
        RaiseError(QUIC_INVALID_FRAME_DATA);
        QUIC_BUG(quic_bug_10850_29) << "QUIC_INVALID_FRAME_DATA";
        return 0;
    }
    ++i;
  }

  if (!WriteIetfLongHeaderLength(header, &writer, length_field_offset, level)) {
    return 0;
  }

  return writer.length();
}

size_t QuicFramer::AppendIetfFrames(const QuicFrames& frames,
                                    QuicDataWriter* writer) {
  size_t i = 0;
  for (const QuicFrame& frame : frames) {
    // Determine if we should write stream frame length in header.
    const bool last_frame_in_packet = i == frames.size() - 1;
    if (!AppendIetfFrameType(frame, last_frame_in_packet, writer)) {
      QUIC_BUG(quic_bug_10850_30)
          << "AppendIetfFrameType failed: " << detailed_error();
      return 0;
    }

    switch (frame.type) {
      case PADDING_FRAME:
        if (!AppendPaddingFrame(frame.padding_frame, writer)) {
          QUIC_BUG(quic_bug_10850_31) << "AppendPaddingFrame of "
                                      << frame.padding_frame.num_padding_bytes
                                      << " failed: " << detailed_error();
          return 0;
        }
        break;
      case STREAM_FRAME:
        if (!AppendStreamFrame(frame.stream_frame, last_frame_in_packet,
                               writer)) {
          QUIC_BUG(quic_bug_10850_32)
              << "AppendStreamFrame " << frame.stream_frame
              << " failed: " << detailed_error();
          return 0;
        }
        break;
      case ACK_FRAME:
        if (!AppendIetfAckFrameAndTypeByte(*frame.ack_frame, writer)) {
          QUIC_BUG(quic_bug_10850_33)
              << "AppendIetfAckFrameAndTypeByte failed: " << detailed_error();
          return 0;
        }
        break;
      case STOP_WAITING_FRAME:
        set_detailed_error(
            "Attempt to append STOP WAITING frame in IETF QUIC.");
        RaiseError(QUIC_INTERNAL_ERROR);
        QUIC_BUG(quic_bug_10850_34) << detailed_error();
        return 0;
      case MTU_DISCOVERY_FRAME:
        // MTU discovery frames are serialized as ping frames.
        ABSL_FALLTHROUGH_INTENDED;
      case PING_FRAME:
        // Ping has no payload.
        break;
      case RST_STREAM_FRAME:
        if (!AppendRstStreamFrame(*frame.rst_stream_frame, writer)) {
          QUIC_BUG(quic_bug_10850_35)
              << "AppendRstStreamFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case CONNECTION_CLOSE_FRAME:
        if (!AppendIetfConnectionCloseFrame(*frame.connection_close_frame,
                                            writer)) {
          QUIC_BUG(quic_bug_10850_36)
              << "AppendIetfConnectionCloseFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case GOAWAY_FRAME:
        set_detailed_error("Attempt to append GOAWAY frame in IETF QUIC.");
        RaiseError(QUIC_INTERNAL_ERROR);
        QUIC_BUG(quic_bug_10850_37) << detailed_error();
        return 0;
      case WINDOW_UPDATE_FRAME:
        // Depending on whether there is a stream ID or not, will be either a
        // MAX STREAM DATA frame or a MAX DATA frame.
        if (frame.window_update_frame.stream_id ==
            QuicUtils::GetInvalidStreamId(transport_version())) {
          if (!AppendMaxDataFrame(frame.window_update_frame, writer)) {
            QUIC_BUG(quic_bug_10850_38)
                << "AppendMaxDataFrame failed: " << detailed_error();
            return 0;
          }
        } else {
          if (!AppendMaxStreamDataFrame(frame.window_update_frame, writer)) {
            QUIC_BUG(quic_bug_10850_39)
                << "AppendMaxStreamDataFrame failed: " << detailed_error();
            return 0;
          }
        }
        break;
      case BLOCKED_FRAME:
        if (!AppendBlockedFrame(frame.blocked_frame, writer)) {
          QUIC_BUG(quic_bug_10850_40)
              << "AppendBlockedFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case MAX_STREAMS_FRAME:
        if (!AppendMaxStreamsFrame(frame.max_streams_frame, writer)) {
          QUIC_BUG(quic_bug_10850_41)
              << "AppendMaxStreamsFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case STREAMS_BLOCKED_FRAME:
        if (!AppendStreamsBlockedFrame(frame.streams_blocked_frame, writer)) {
          QUIC_BUG(quic_bug_10850_42)
              << "AppendStreamsBlockedFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case NEW_CONNECTION_ID_FRAME:
        if (!AppendNewConnectionIdFrame(*frame.new_connection_id_frame,
                                        writer)) {
          QUIC_BUG(quic_bug_10850_43)
              << "AppendNewConnectionIdFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case RETIRE_CONNECTION_ID_FRAME:
        if (!AppendRetireConnectionIdFrame(*frame.retire_connection_id_frame,
                                           writer)) {
          QUIC_BUG(quic_bug_10850_44)
              << "AppendRetireConnectionIdFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case NEW_TOKEN_FRAME:
        if (!AppendNewTokenFrame(*frame.new_token_frame, writer)) {
          QUIC_BUG(quic_bug_10850_45)
              << "AppendNewTokenFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case STOP_SENDING_FRAME:
        if (!AppendStopSendingFrame(frame.stop_sending_frame, writer)) {
          QUIC_BUG(quic_bug_10850_46)
              << "AppendStopSendingFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case PATH_CHALLENGE_FRAME:
        if (!AppendPathChallengeFrame(frame.path_challenge_frame, writer)) {
          QUIC_BUG(quic_bug_10850_47)
              << "AppendPathChallengeFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case PATH_RESPONSE_FRAME:
        if (!AppendPathResponseFrame(frame.path_response_frame, writer)) {
          QUIC_BUG(quic_bug_10850_48)
              << "AppendPathResponseFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case MESSAGE_FRAME:
        if (!AppendMessageFrameAndTypeByte(*frame.message_frame,
                                           last_frame_in_packet, writer)) {
          QUIC_BUG(quic_bug_10850_49)
              << "AppendMessageFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case CRYPTO_FRAME:
        if (!AppendCryptoFrame(*frame.crypto_frame, writer)) {
          QUIC_BUG(quic_bug_10850_50)
              << "AppendCryptoFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case HANDSHAKE_DONE_FRAME:
        // HANDSHAKE_DONE has no payload.
        break;
      case ACK_FREQUENCY_FRAME:
        if (!AppendAckFrequencyFrame(*frame.ack_frequency_frame, writer)) {
          QUIC_BUG(quic_bug_10850_51)
              << "AppendAckFrequencyFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case IMMEDIATE_ACK_FRAME:
        // IMMEDIATE_ACK has no payload.
        break;
      case RESET_STREAM_AT_FRAME:
        QUIC_BUG_IF(reset_stream_at_appended_while_disabled,
                    !process_reset_stream_at_)
            << "Requested serialization of RESET_STREAM_AT_FRAME while it is "
               "not explicitly enabled in the framer";
        if (!AppendResetFrameAtFrame(*frame.reset_stream_at_frame, *writer)) {
          QUIC_BUG(cannot_append_reset_stream_at)
              << "AppendResetStreamAtFram failed: " << detailed_error();
          return 0;
        }
        break;
      default:
        set_detailed_error("Tried to append unknown frame type.");
        RaiseError(QUIC_INVALID_FRAME_DATA);
        QUIC_BUG(quic_bug_10850_52)
            << "QUIC_INVALID_FRAME_DATA: " << frame.type;
        return 0;
    }
    ++i;
  }

  return writer->length();
}

// static
std::unique_ptr<QuicEncryptedPacket> QuicFramer::BuildPublicResetPacket(
    const QuicPublicResetPacket& packet) {
  CryptoHandshakeMessage reset;
  reset.set_tag(kPRST);
  reset.SetValue(kRNON, packet.nonce_proof);
  if (packet.client_address.host().address_family() !=
      IpAddressFamily::IP_UNSPEC) {
    // packet.client_address is non-empty.
    QuicSocketAddressCoder address_coder(packet.client_address);
    std::string serialized_address = address_coder.Encode();
    if (serialized_address.empty()) {
      return nullptr;
    }
    reset.SetStringPiece(kCADR, serialized_address);
  }
  if (!packet.endpoint_id.empty()) {
    reset.SetStringPiece(kEPID, packet.endpoint_id);
  }
  const QuicData& reset_serialized = reset.GetSerialized();

  size_t len = kPublicFlagsSize + packet.connection_id.length() +
               reset_serialized.length();
  std::unique_ptr<char[]> buffer(new char[len]);
  QuicDataWriter writer(len, buffer.get());

  uint8_t flags = static_cast<uint8_t>(PACKET_PUBLIC_FLAGS_RST |
                                       PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID);
  // This hack makes post-v33 public reset packet look like pre-v33 packets.
  flags |= static_cast<uint8_t>(PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID_OLD);
  if (!writer.WriteUInt8(flags)) {
    return nullptr;
  }

  if (!writer.WriteConnectionId(packet.connection_id)) {
    return nullptr;
  }

  if (!writer.WriteBytes(reset_serialized.data(), reset_serialized.length())) {
    return nullptr;
  }

  return std::make_unique<QuicEncryptedPacket>(buffer.release(), len, true);
}

// static
size_t QuicFramer::GetMinStatelessResetPacketLength() {
  // 5 bytes (40 bits) = 2 Fixed Bits (01) + 38 Unpredictable bits
  return 5 + kStatelessResetTokenLength;
}

// static
std::unique_ptr<QuicEncryptedPacket> QuicFramer::BuildIetfStatelessResetPacket(
    QuicConnectionId connection_id, size_t received_packet_length,
    StatelessResetToken stateless_reset_token) {
  return BuildIetfStatelessResetPacket(connection_id, received_packet_length,
                                       stateless_reset_token,
                                       QuicRandom::GetInstance());
}

// static
std::unique_ptr<QuicEncryptedPacket> QuicFramer::BuildIetfStatelessResetPacket(
    QuicConnectionId /*connection_id*/, size_t received_packet_length,
    StatelessResetToken stateless_reset_token, QuicRandom* random) {
  QUIC_DVLOG(1) << "Building IETF stateless reset packet.";
  if (received_packet_length <= GetMinStatelessResetPacketLength()) {
    QUICHE_DLOG(ERROR)
        << "Tried to build stateless reset packet with received packet "
           "length "
        << received_packet_length;
    return nullptr;
  }
  // To ensure stateless reset is indistinguishable from a valid packet,
  // include the max connection ID length.
  size_t len = std::min(received_packet_length - 1,
                        GetMinStatelessResetPacketLength() + 1 +
                            kQuicMaxConnectionIdWithLengthPrefixLength);
  std::unique_ptr<char[]> buffer(new char[len]);
  QuicDataWriter writer(len, buffer.get());
  // Append random bytes. This randomness only exists to prevent middleboxes
  // from comparing the entire packet to a known value. Therefore it has no
  // cryptographic use, and does not need a secure cryptographic pseudo-random
  // number generator. It's therefore safe to use WriteInsecureRandomBytes.
  const size_t random_bytes_size = len - kStatelessResetTokenLength;
  if (!writer.WriteInsecureRandomBytes(random, random_bytes_size)) {
    QUIC_BUG(362045737_2) << "Failed to append random bytes of length: "
                          << random_bytes_size;
    return nullptr;
  }
  // Change first 2 fixed bits to 01.
  buffer[0] &= ~FLAGS_LONG_HEADER;
  buffer[0] |= FLAGS_FIXED_BIT;

  // Append stateless reset token.
  if (!writer.WriteBytes(&stateless_reset_token,
                         sizeof(stateless_reset_token))) {
    QUIC_BUG(362045737_3) << "Failed to write stateless reset token";
    return nullptr;
  }
  return std::make_unique<QuicEncryptedPacket>(buffer.release(), len,
                                               /*owns_buffer=*/true);
}

// static
std::unique_ptr<QuicEncryptedPacket> QuicFramer::BuildVersionNegotiationPacket(
    QuicConnectionId server_connection_id,
    QuicConnectionId client_connection_id, bool ietf_quic,
    bool use_length_prefix, const ParsedQuicVersionVector& versions) {
  QUIC_CODE_COUNT(quic_build_version_negotiation);
  if (use_length_prefix) {
    QUICHE_DCHECK(ietf_quic);
    QUIC_CODE_COUNT(quic_build_version_negotiation_ietf);
  } else if (ietf_quic) {
    QUIC_CODE_COUNT(quic_build_version_negotiation_old_ietf);
  } else {
    QUIC_CODE_COUNT(quic_build_version_negotiation_old_gquic);
  }
  ParsedQuicVersionVector wire_versions = versions;
  // Add a version reserved for negotiation as suggested by the
  // "Using Reserved Versions" section of draft-ietf-quic-transport.
  if (wire_versions.empty()) {
    // Ensure that version negotiation packets we send have at least two
    // versions. This guarantees that, under all circumstances, all QUIC
    // packets we send are at least 14 bytes long.
    wire_versions = {QuicVersionReservedForNegotiation(),
                     QuicVersionReservedForNegotiation()};
  } else {
    // This is not uniformely distributed but is acceptable since no security
    // depends on this randomness.
    size_t version_index = 0;
    const bool disable_randomness =
        GetQuicFlag(quic_disable_version_negotiation_grease_randomness);
    if (!disable_randomness) {
      version_index =
          QuicRandom::GetInstance()->RandUint64() % (wire_versions.size() + 1);
    }
    wire_versions.insert(wire_versions.begin() + version_index,
                         QuicVersionReservedForNegotiation());
  }
  if (ietf_quic) {
    return BuildIetfVersionNegotiationPacket(
        use_length_prefix, server_connection_id, client_connection_id,
        wire_versions);
  }

  // The GQUIC encoding does not support encoding client connection IDs.
  QUICHE_DCHECK(client_connection_id.IsEmpty());
  // The GQUIC encoding does not support length-prefixed connection IDs.
  QUICHE_DCHECK(!use_length_prefix);

  QUICHE_DCHECK(!wire_versions.empty());
  size_t len = kPublicFlagsSize + server_connection_id.length() +
               wire_versions.size() * kQuicVersionSize;
  std::unique_ptr<char[]> buffer(new char[len]);
  QuicDataWriter writer(len, buffer.get());

  uint8_t flags = static_cast<uint8_t>(
      PACKET_PUBLIC_FLAGS_VERSION | PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID |
      PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID_OLD);
  if (!writer.WriteUInt8(flags)) {
    return nullptr;
  }

  if (!writer.WriteConnectionId(server_connection_id)) {
    return nullptr;
  }

  for (const ParsedQuicVersion& version : wire_versions) {
    if (!writer.WriteUInt32(CreateQuicVersionLabel(version))) {
      return nullptr;
    }
  }

  return std::make_unique<QuicEncryptedPacket>(buffer.release(), len, true);
}

// static
std::unique_ptr<QuicEncryptedPacket>
QuicFramer::BuildIetfVersionNegotiationPacket(
    bool use_length_prefix, QuicConnectionId server_connection_id,
    QuicConnectionId client_connection_id,
    const ParsedQuicVersionVector& versions) {
  QUIC_DVLOG(1) << "Building IETF version negotiation packet with"
                << (use_length_prefix ? "" : "out")
                << " length prefix, server_connection_id "
                << server_connection_id << " client_connection_id "
                << client_connection_id << " versions "
                << ParsedQuicVersionVectorToString(versions);
  QUICHE_DCHECK(!versions.empty());
  size_t len = kPacketHeaderTypeSize + kConnectionIdLengthSize +
               client_connection_id.length() + server_connection_id.length() +
               (versions.size() + 1) * kQuicVersionSize;
  if (use_length_prefix) {
    // When using length-prefixed connection IDs, packets carry two lengths
    // instead of one.
    len += kConnectionIdLengthSize;
  }
  std::unique_ptr<char[]> buffer(new char[len]);
  QuicDataWriter writer(len, buffer.get());

  // TODO(fayang): Randomly select a value for the type.
  uint8_t type = static_cast<uint8_t>(FLAGS_LONG_HEADER | FLAGS_FIXED_BIT);
  if (!writer.WriteUInt8(type)) {
    return nullptr;
  }

  if (!writer.WriteUInt32(0)) {
    return nullptr;
  }

  if (!AppendIetfConnectionIds(true, use_length_prefix, client_connection_id,
                               server_connection_id, &writer)) {
    return nullptr;
  }

  for (const ParsedQuicVersion& version : versions) {
    if (!writer.WriteUInt32(CreateQuicVersionLabel(version))) {
      return nullptr;
    }
  }

  return std::make_unique<QuicEncryptedPacket>(buffer.release(), len, true);
}

bool QuicFramer::ProcessPacket(const QuicEncryptedPacket& packet) {
  QUICHE_DCHECK(!is_processing_packet_) << ENDPOINT << "Nested ProcessPacket";
  is_processing_packet_ = true;
  bool result = ProcessPacketInternal(packet);
  is_processing_packet_ = false;
  return result;
}

bool QuicFramer::ProcessPacketInternal(const QuicEncryptedPacket& packet) {
  QuicDataReader reader(packet.data(), packet.length());
  QUIC_DVLOG(1) << ENDPOINT << "Processing IETF QUIC packet.";

  visitor_->OnPacket();

  QuicPacketHeader header;
  if (!ProcessIetfPacketHeader(&reader, &header)) {
    QUICHE_DCHECK_NE("", detailed_error_);
    QUIC_DVLOG(1) << ENDPOINT << "Unable to process public header. Error: "
                  << detailed_error_;
    QUICHE_DCHECK_NE("", detailed_error_);
    RecordDroppedPacketReason(DroppedPacketReason::INVALID_PUBLIC_HEADER);
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }

  if (!visitor_->OnUnauthenticatedPublicHeader(header)) {
    // The visitor suppresses further processing of the packet.
    return true;
  }

  if (IsVersionNegotiation(header)) {
    if (perspective_ == Perspective::IS_CLIENT) {
      QUIC_DVLOG(1) << "Client received version negotiation packet";
      return ProcessVersionNegotiationPacket(&reader, header);
    } else {
      QUIC_DLOG(ERROR) << "Server received version negotiation packet";
      set_detailed_error("Server received version negotiation packet.");
      return RaiseError(QUIC_INVALID_VERSION_NEGOTIATION_PACKET);
    }
  }

  if (header.version_flag && header.version != version_) {
    if (perspective_ == Perspective::IS_SERVER) {
      if (!visitor_->OnProtocolVersionMismatch(header.version)) {
        RecordDroppedPacketReason(DroppedPacketReason::VERSION_MISMATCH);
        return true;
      }
    } else {
      // A client received a packet of a different version but that packet is
      // not a version negotiation packet. It is therefore invalid and dropped.
      QUIC_DLOG(ERROR) << "Client received unexpected version "
                       << ParsedQuicVersionToString(header.version)
                       << " instead of " << ParsedQuicVersionToString(version_);
      set_detailed_error("Client received unexpected version.");
      return RaiseError(QUIC_PACKET_WRONG_VERSION);
    }
  }

  bool rv;
  if (header.long_packet_type == RETRY) {
    rv = ProcessRetryPacket(&reader, header);
  } else if (packet.length() <= kMaxIncomingPacketSize) {
    // The optimized decryption algorithm implementations run faster when
    // operating on aligned memory.
    ABSL_CACHELINE_ALIGNED char buffer[kMaxIncomingPacketSize];
    rv = ProcessIetfDataPacket(&reader, &header, packet, buffer,
                               ABSL_ARRAYSIZE(buffer));
  } else {
    std::unique_ptr<char[]> large_buffer(new char[packet.length()]);
    rv = ProcessIetfDataPacket(&reader, &header, packet, large_buffer.get(),
                               packet.length());
    QUIC_BUG_IF(quic_bug_10850_53, rv)
        << "QUIC should never successfully process packets larger"
        << "than kMaxIncomingPacketSize. packet size:" << packet.length();
  }
  return rv;
}

bool QuicFramer::ProcessVersionNegotiationPacket(
    QuicDataReader* reader, const QuicPacketHeader& header) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);

  QuicVersionNegotiationPacket packet(
      GetServerConnectionIdAsRecipient(header, perspective_));
  // Try reading at least once to raise error if the packet is invalid.
  do {
    QuicVersionLabel version_label;
    if (!ProcessVersionLabel(reader, &version_label)) {
      set_detailed_error("Unable to read supported version in negotiation.");
      RecordDroppedPacketReason(
          DroppedPacketReason::INVALID_VERSION_NEGOTIATION_PACKET);
      return RaiseError(QUIC_INVALID_VERSION_NEGOTIATION_PACKET);
    }
    ParsedQuicVersion parsed_version = ParseQuicVersionLabel(version_label);
    if (parsed_version != UnsupportedQuicVersion()) {
      packet.versions.push_back(parsed_version);
    }
  } while (!reader->IsDoneReading());

  QUIC_DLOG(INFO) << ENDPOINT << "parsed version negotiation: "
                  << ParsedQuicVersionVectorToString(packet.versions);

  visitor_->OnVersionNegotiationPacket(packet);
  return true;
}

bool QuicFramer::ProcessRetryPacket(QuicDataReader* reader,
                                    const QuicPacketHeader& header) {
  QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
  if (drop_incoming_retry_packets_) {
    QUIC_DLOG(INFO) << "Ignoring received RETRY packet";
    return true;
  }

  if (version_.UsesTls()) {
    QUICHE_DCHECK(version_.HasLengthPrefixedConnectionIds()) << version_;
    const size_t bytes_remaining = reader->BytesRemaining();
    if (bytes_remaining <= kRetryIntegrityTagLength) {
      set_detailed_error("Retry packet too short to parse integrity tag.");
      return false;
    }
    const size_t retry_token_length =
        bytes_remaining - kRetryIntegrityTagLength;
    QUICHE_DCHECK_GT(retry_token_length, 0u);
    absl::string_view retry_token;
    if (!reader->ReadStringPiece(&retry_token, retry_token_length)) {
      set_detailed_error("Failed to read retry token.");
      return false;
    }
    absl::string_view retry_without_tag = reader->PreviouslyReadPayload();
    absl::string_view integrity_tag = reader->ReadRemainingPayload();
    QUICHE_DCHECK_EQ(integrity_tag.length(), kRetryIntegrityTagLength);
    visitor_->OnRetryPacket(EmptyQuicConnectionId(),
                            header.source_connection_id, retry_token,
                            integrity_tag, retry_without_tag);
    return true;
  }

  QuicConnectionId original_destination_connection_id;
  if (version_.HasLengthPrefixedConnectionIds()) {
    // Parse Original Destination Connection ID.
    if (!reader->ReadLengthPrefixedConnectionId(
            &original_destination_connection_id)) {
      set_detailed_error("Unable to read Original Destination ConnectionId.");
      return false;
    }
  } else {
    // Parse Original Destination Connection ID Length.
    uint8_t odcil = header.type_byte & 0xf;
    if (odcil != 0) {
      odcil += kConnectionIdLengthAdjustment;
    }

    // Parse Original Destination Connection ID.
    if (!reader->ReadConnectionId(&original_destination_connection_id, odcil)) {
      set_detailed_error("Unable to read Original Destination ConnectionId.");
      return false;
    }
  }

  if (!QuicUtils::IsConnectionIdValidForVersion(
          original_destination_connection_id, transport_version())) {
    set_detailed_error(
        "Received Original Destination ConnectionId with invalid length.");
    return false;
  }

  absl::string_view retry_token = reader->ReadRemainingPayload();
  visitor_->OnRetryPacket(original_destination_connection_id,
                          header.source_connection_id, retry_token,
                          /*retry_integrity_tag=*/absl::string_view(),
                          /*retry_without_tag=*/absl::string_view());
  return true;
}

// Seeks the current packet to check for a coalesced packet at the end.
// If the IETF length field only spans part of the outer packet,
// then there is a coalesced packet after this one.
void QuicFramer::MaybeProcessCoalescedPacket(
    const QuicDataReader& encrypted_reader, uint64_t remaining_bytes_length,
    const QuicPacketHeader& header) {
  if (header.remaining_packet_length >= remaining_bytes_length) {
    // There is no coalesced packet.
    return;
  }

  absl::string_view remaining_data = encrypted_reader.PeekRemainingPayload();
  QUICHE_DCHECK_EQ(remaining_data.length(), remaining_bytes_length);

  const char* coalesced_data =
      remaining_data.data() + header.remaining_packet_length;
  uint64_t coalesced_data_length =
      remaining_bytes_length - header.remaining_packet_length;
  QuicDataReader coalesced_reader(coalesced_data, coalesced_data_length);

  QuicPacketHeader coalesced_header;
  if (!ProcessIetfPacketHeader(&coalesced_reader, &coalesced_header)) {
    // Some implementations pad their INITIAL packets by sending random invalid
    // data after the INITIAL, and that is allowed by the specification. If we
    // fail to parse a subsequent coalesced packet, simply ignore it.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Failed to parse received coalesced header of length "
                    << coalesced_data_length
                    << " with error: " << detailed_error_ << ": "
                    << absl::BytesToHexString(absl::string_view(
                           coalesced_data, coalesced_data_length))
                    << " previous header was " << header;
    return;
  }

  if (coalesced_header.destination_connection_id !=
      header.destination_connection_id) {
    // Drop coalesced packets with mismatched connection IDs.
    QUIC_DLOG(INFO) << ENDPOINT << "Received mismatched coalesced header "
                    << coalesced_header << " previous header was " << header;
    QUIC_CODE_COUNT(
        quic_received_coalesced_packets_with_mismatched_connection_id);
    return;
  }

  QuicEncryptedPacket coalesced_packet(coalesced_data, coalesced_data_length,
                                       /*owns_buffer=*/false);
  visitor_->OnCoalescedPacket(coalesced_packet);
}

bool QuicFramer::MaybeProcessIetfLength(QuicDataReader* encrypted_reader,
                                        QuicPacketHeader* header) {
  if (!QuicVersionHasLongHeaderLengths(header->version.transport_version) ||
      header->form != IETF_QUIC_LONG_HEADER_PACKET ||
      (header->long_packet_type != INITIAL &&
       header->long_packet_type != HANDSHAKE &&
       header->long_packet_type != ZERO_RTT_PROTECTED)) {
    return true;
  }
  header->length_length = encrypted_reader->PeekVarInt62Length();
  if (!encrypted_reader->ReadVarInt62(&header->remaining_packet_length)) {
    set_detailed_error("Unable to read long header payload length.");
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }
  uint64_t remaining_bytes_length = encrypted_reader->BytesRemaining();
  if (header->remaining_packet_length > remaining_bytes_length) {
    set_detailed_error("Long header payload length longer than packet.");
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }

  MaybeProcessCoalescedPacket(*encrypted_reader, remaining_bytes_length,
                              *header);

  if (!encrypted_reader->TruncateRemaining(header->remaining_packet_length)) {
    set_detailed_error("Length TruncateRemaining failed.");
    QUIC_BUG(quic_bug_10850_54) << "Length TruncateRemaining failed.";
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }
  return true;
}

bool QuicFramer::ProcessIetfDataPacket(QuicDataReader* encrypted_reader,
                                       QuicPacketHeader* header,
                                       const QuicEncryptedPacket& packet,
                                       char* decrypted_buffer,
                                       size_t buffer_length) {
  QUICHE_DCHECK_NE(GOOGLE_QUIC_PACKET, header->form);
  QUICHE_DCHECK(!header->has_possible_stateless_reset_token);
  header->length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
  header->remaining_packet_length = 0;
  if (header->form == IETF_QUIC_SHORT_HEADER_PACKET &&
      perspective_ == Perspective::IS_CLIENT) {
    // Peek possible stateless reset token. Will only be used on decryption
    // failure.
    absl::string_view remaining = encrypted_reader->PeekRemainingPayload();
    if (remaining.length() >= sizeof(header->possible_stateless_reset_token)) {
      header->has_possible_stateless_reset_token = true;
      memcpy(&header->possible_stateless_reset_token,
             &remaining.data()[remaining.length() -
                               sizeof(header->possible_stateless_reset_token)],
             sizeof(header->possible_stateless_reset_token));
    }
  }

  if (!MaybeProcessIetfLength(encrypted_reader, header)) {
    return false;
  }

  absl::string_view associated_data;
  AssociatedDataStorage ad_storage;
  QuicPacketNumber base_packet_number;
  if (header->form == IETF_QUIC_SHORT_HEADER_PACKET ||
      header->long_packet_type != VERSION_NEGOTIATION) {
    QUICHE_DCHECK(header->form == IETF_QUIC_SHORT_HEADER_PACKET ||
                  header->long_packet_type == INITIAL ||
                  header->long_packet_type == HANDSHAKE ||
                  header->long_packet_type == ZERO_RTT_PROTECTED);
    // Process packet number.
    if (supports_multiple_packet_number_spaces_) {
      PacketNumberSpace pn_space = GetPacketNumberSpace(*header);
      if (pn_space == NUM_PACKET_NUMBER_SPACES) {
        return RaiseError(QUIC_INVALID_PACKET_HEADER);
      }
      base_packet_number = largest_decrypted_packet_numbers_[pn_space];
    } else {
      base_packet_number = largest_packet_number_;
    }
    uint64_t full_packet_number;
    bool hp_removal_failed = false;
    if (version_.HasHeaderProtection()) {
      EncryptionLevel expected_decryption_level = GetEncryptionLevel(*header);
      QuicDecrypter* decrypter = decrypter_[expected_decryption_level].get();
      if (decrypter == nullptr) {
        QUIC_DVLOG(1)
            << ENDPOINT
            << "No decrypter available for removing header protection at level "
            << expected_decryption_level;
        hp_removal_failed = true;
      } else if (!RemoveHeaderProtection(encrypted_reader, packet, *decrypter,
                                         perspective_, version_,
                                         base_packet_number, header,
                                         &full_packet_number, ad_storage)) {
        hp_removal_failed = true;
      }
      associated_data = absl::string_view(ad_storage.data(), ad_storage.size());
    } else if (!ProcessAndCalculatePacketNumber(
                   encrypted_reader, header->packet_number_length,
                   base_packet_number, &full_packet_number)) {
      set_detailed_error("Unable to read packet number.");
      RecordDroppedPacketReason(DroppedPacketReason::INVALID_PACKET_NUMBER);
      return RaiseError(QUIC_INVALID_PACKET_HEADER);
    }

    if (hp_removal_failed ||
        !IsValidFullPacketNumber(full_packet_number, version())) {
      if (IsIetfStatelessResetPacket(*header)) {
        // This is a stateless reset packet.
        QuicIetfStatelessResetPacket reset_packet(
            *header, header->possible_stateless_reset_token);
        visitor_->OnAuthenticatedIetfStatelessResetPacket(reset_packet);
        return true;
      }
      if (hp_removal_failed) {
        const EncryptionLevel decryption_level = GetEncryptionLevel(*header);
        const bool has_decryption_key = decrypter_[decryption_level] != nullptr;
        visitor_->OnUndecryptablePacket(
            QuicEncryptedPacket(encrypted_reader->FullPayload()),
            decryption_level, has_decryption_key);
        RecordDroppedPacketReason(DroppedPacketReason::DECRYPTION_FAILURE);
        set_detailed_error(absl::StrCat(
            "Unable to decrypt ", EncryptionLevelToString(decryption_level),
            " header protection", has_decryption_key ? "" : " (missing key)",
            "."));
        return RaiseError(QUIC_DECRYPTION_FAILURE);
      }
      RecordDroppedPacketReason(DroppedPacketReason::INVALID_PACKET_NUMBER);
      set_detailed_error("packet numbers cannot be 0.");
      return RaiseError(QUIC_INVALID_PACKET_HEADER);
    }
    header->packet_number = QuicPacketNumber(full_packet_number);
  }

  // A nonce should only present in SHLO from the server to the client when
  // using QUIC crypto.
  if (header->form == IETF_QUIC_LONG_HEADER_PACKET &&
      header->long_packet_type == ZERO_RTT_PROTECTED &&
      perspective_ == Perspective::IS_CLIENT &&
      version_.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
    if (!encrypted_reader->ReadBytes(
            reinterpret_cast<uint8_t*>(last_nonce_.data()),
            last_nonce_.size())) {
      set_detailed_error("Unable to read nonce.");
      RecordDroppedPacketReason(
          DroppedPacketReason::INVALID_DIVERSIFICATION_NONCE);
      return RaiseError(QUIC_INVALID_PACKET_HEADER);
    }

    header->nonce = &last_nonce_;
  } else {
    header->nonce = nullptr;
  }

  if (!visitor_->OnUnauthenticatedHeader(*header)) {
    set_detailed_error(
        "Visitor asked to stop processing of unauthenticated header.");
    return false;
  }

  absl::string_view encrypted = encrypted_reader->ReadRemainingPayload();
  if (!version_.HasHeaderProtection()) {
    associated_data = GetAssociatedDataFromEncryptedPacket(
        version_.transport_version, packet,
        GetIncludedDestinationConnectionIdLength(*header),
        GetIncludedSourceConnectionIdLength(*header), header->version_flag,
        header->nonce != nullptr, header->packet_number_length,
        header->retry_token_length_length, header->retry_token.length(),
        header->length_length);
  }

  size_t decrypted_length = 0;
  EncryptionLevel decrypted_level;
  if (!DecryptPayload(packet.length(), encrypted, associated_data, *header,
                      decrypted_buffer, buffer_length, &decrypted_length,
                      &decrypted_level)) {
    if (IsIetfStatelessResetPacket(*header)) {
      // This is a stateless reset packet.
      QuicIetfStatelessResetPacket reset_packet(
          *header, header->possible_stateless_reset_token);
      visitor_->OnAuthenticatedIetfStatelessResetPacket(reset_packet);
      return true;
    }
    const EncryptionLevel decryption_level = GetEncryptionLevel(*header);
    const bool has_decryption_key = version_.KnowsWhichDecrypterToUse() &&
                                    decrypter_[decryption_level] != nullptr;
    visitor_->OnUndecryptablePacket(
        QuicEncryptedPacket(encrypted_reader->FullPayload()), decryption_level,
        has_decryption_key);
    set_detailed_error(absl::StrCat(
        "Unable to decrypt ", EncryptionLevelToString(decryption_level),
        " payload with reconstructed packet number ",
        header->packet_number.ToString(), " (largest decrypted was ",
        base_packet_number.ToString(), ")",
        has_decryption_key || !version_.KnowsWhichDecrypterToUse()
            ? ""
            : " (missing key)",
        "."));
    RecordDroppedPacketReason(DroppedPacketReason::DECRYPTION_FAILURE);
    return RaiseError(QUIC_DECRYPTION_FAILURE);
  }

  if (packet.length() > kMaxIncomingPacketSize) {
    set_detailed_error("Packet too large.");
    return RaiseError(QUIC_PACKET_TOO_LARGE);
  }

  QuicDataReader reader(decrypted_buffer, decrypted_length);

  // Update the largest packet number after we have decrypted the packet
  // so we are confident is not attacker controlled.
  if (supports_multiple_packet_number_spaces_) {
    largest_decrypted_packet_numbers_[QuicUtils::GetPacketNumberSpace(
                                          decrypted_level)]
        .UpdateMax(header->packet_number);
  } else {
    largest_packet_number_.UpdateMax(header->packet_number);
  }

  if (!visitor_->OnPacketHeader(*header)) {
    RecordDroppedPacketReason(DroppedPacketReason::INVALID_PACKET_NUMBER);
    // The visitor suppresses further processing of the packet.
    return true;
  }

  // Handle the payload.
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    current_received_frame_type_ = 0;
    previously_received_frame_type_ = 0;
    if (!ProcessIetfFrameData(&reader, *header, decrypted_level)) {
      current_received_frame_type_ = 0;
      previously_received_frame_type_ = 0;
      QUICHE_DCHECK_NE(QUIC_NO_ERROR,
                       error_);  // ProcessIetfFrameData sets the error.
      QUICHE_DCHECK_NE("", detailed_error_);
      QUIC_DLOG(WARNING) << ENDPOINT << "Unable to process frame data. Error: "
                         << detailed_error_;
      return false;
    }
    current_received_frame_type_ = 0;
    previously_received_frame_type_ = 0;
  } else {
    if (!ProcessFrameData(&reader, *header)) {
      QUICHE_DCHECK_NE(QUIC_NO_ERROR,
                       error_);  // ProcessFrameData sets the error.
      QUICHE_DCHECK_NE("", detailed_error_);
      QUIC_DLOG(WARNING) << ENDPOINT << "Unable to process frame data. Error: "
                         << detailed_error_;
      return false;
    }
  }

  visitor_->OnPacketComplete();
  return true;
}

bool QuicFramer::IsIetfStatelessResetPacket(
    const QuicPacketHeader& header) const {
  QUIC_BUG_IF(quic_bug_12975_3, header.has_possible_stateless_reset_token &&
                                    perspective_ != Perspective::IS_CLIENT)
      << "has_possible_stateless_reset_token can only be true at client side.";
  return header.form == IETF_QUIC_SHORT_HEADER_PACKET &&
         header.has_possible_stateless_reset_token &&
         visitor_->IsValidStatelessResetToken(
             header.possible_stateless_reset_token);
}

bool QuicFramer::HasEncrypterOfEncryptionLevel(EncryptionLevel level) const {
  return encrypter_[level] != nullptr;
}

bool QuicFramer::HasDecrypterOfEncryptionLevel(EncryptionLevel level) const {
  return decrypter_[level] != nullptr;
}

bool QuicFramer::HasAnEncrypterForSpace(PacketNumberSpace space) const {
  switch (space) {
    case INITIAL_DATA:
      return HasEncrypterOfEncryptionLevel(ENCRYPTION_INITIAL);
    case HANDSHAKE_DATA:
      return HasEncrypterOfEncryptionLevel(ENCRYPTION_HANDSHAKE);
    case APPLICATION_DATA:
      return HasEncrypterOfEncryptionLevel(ENCRYPTION_ZERO_RTT) ||
             HasEncrypterOfEncryptionLevel(ENCRYPTION_FORWARD_SECURE);
    case NUM_PACKET_NUMBER_SPACES:
      break;
  }
  QUIC_BUG(quic_bug_10850_55)
      << ENDPOINT
      << "Try to send data of space: " << PacketNumberSpaceToString(space);
  return false;
}

EncryptionLevel QuicFramer::GetEncryptionLevelToSendApplicationData() const {
  if (!HasAnEncrypterForSpace(APPLICATION_DATA)) {
    QUIC_BUG(quic_bug_12975_4)
        << "Tried to get encryption level to send application data with no "
           "encrypter available.";
    return NUM_ENCRYPTION_LEVELS;
  }
  if (HasEncrypterOfEncryptionLevel(ENCRYPTION_FORWARD_SECURE)) {
    return ENCRYPTION_FORWARD_SECURE;
  }
  QUICHE_DCHECK(HasEncrypterOfEncryptionLevel(ENCRYPTION_ZERO_RTT));
  return ENCRYPTION_ZERO_RTT;
}

bool QuicFramer::AppendIetfHeaderTypeByte(const QuicPacketHeader& header,
                                          QuicDataWriter* writer) {
  uint8_t type = 0;
  if (header.version_flag) {
    type = static_cast<uint8_t>(
        FLAGS_LONG_HEADER | FLAGS_FIXED_BIT |
        LongHeaderTypeToOnWireValue(header.long_packet_type, version_) |
        PacketNumberLengthToOnWireValue(header.packet_number_length));
  } else {
    type = static_cast<uint8_t>(
        FLAGS_FIXED_BIT | (current_key_phase_bit_ ? FLAGS_KEY_PHASE_BIT : 0) |
        PacketNumberLengthToOnWireValue(header.packet_number_length));
  }
  return writer->WriteUInt8(type);
}

bool QuicFramer::AppendIetfPacketHeader(const QuicPacketHeader& header,
                                        QuicDataWriter* writer,
                                        size_t* length_field_offset) {
  QUIC_DVLOG(1) << ENDPOINT << "Appending IETF header: " << header;
  QuicConnectionId server_connection_id =
      GetServerConnectionIdAsSender(header, perspective_);
  QUIC_BUG_IF(quic_bug_12975_6, !QuicUtils::IsConnectionIdValidForVersion(
                                    server_connection_id, transport_version()))
      << "AppendIetfPacketHeader: attempted to use connection ID "
      << server_connection_id << " which is invalid with version " << version();
  if (!AppendIetfHeaderTypeByte(header, writer)) {
    return false;
  }

  if (header.version_flag) {
    QUICHE_DCHECK_NE(VERSION_NEGOTIATION, header.long_packet_type)
        << "QuicFramer::AppendIetfPacketHeader does not support sending "
           "version negotiation packets, use "
           "QuicFramer::BuildVersionNegotiationPacket instead "
        << header;
    // Append version for long header.
    QuicVersionLabel version_label = CreateQuicVersionLabel(version_);
    if (!writer->WriteUInt32(version_label)) {
      return false;
    }
  }

  // Append connection ID.
  if (!AppendIetfConnectionIds(
          header.version_flag, version_.HasLengthPrefixedConnectionIds(),
          header.destination_connection_id_included != CONNECTION_ID_ABSENT
              ? header.destination_connection_id
              : EmptyQuicConnectionId(),
          header.source_connection_id_included != CONNECTION_ID_ABSENT
              ? header.source_connection_id
              : EmptyQuicConnectionId(),
          writer)) {
    return false;
  }

  last_serialized_server_connection_id_ = server_connection_id;

  // TODO(b/141924462) Remove this QUIC_BUG once we do support sending RETRY.
  QUIC_BUG_IF(quic_bug_12975_7,
              header.version_flag && header.long_packet_type == RETRY)
      << "Sending IETF RETRY packets is not currently supported " << header;

  if (QuicVersionHasLongHeaderLengths(transport_version()) &&
      header.version_flag) {
    if (header.long_packet_type == INITIAL) {
      QUICHE_DCHECK_NE(quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0,
                       header.retry_token_length_length)
          << ENDPOINT << ParsedQuicVersionToString(version_)
          << " bad retry token length length in header: " << header;
      // Write retry token length.
      if (!writer->WriteVarInt62WithForcedLength(
              header.retry_token.length(), header.retry_token_length_length)) {
        return false;
      }
      // Write retry token.
      if (!header.retry_token.empty() &&
          !writer->WriteStringPiece(header.retry_token)) {
        return false;
      }
    }
    if (length_field_offset != nullptr) {
      *length_field_offset = writer->length();
    }
    // Add fake length to reserve two bytes to add length in later.
    writer->WriteVarInt62(256);
  } else if (length_field_offset != nullptr) {
    *length_field_offset = 0;
  }

  // Append packet number.
  if (!AppendPacketNumber(header.packet_number_length, header.packet_number,
                          writer)) {
    return false;
  }
  last_written_packet_number_length_ = header.packet_number_length;

  if (!header.version_flag) {
    return true;
  }

  if (header.nonce != nullptr) {
    QUICHE_DCHECK(header.version_flag);
    QUICHE_DCHECK_EQ(ZERO_RTT_PROTECTED, header.long_packet_type);
    QUICHE_DCHECK_EQ(Perspective::IS_SERVER, perspective_);
    if (!writer->WriteBytes(header.nonce, kDiversificationNonceSize)) {
      return false;
    }
  }

  return true;
}

const QuicTime::Delta QuicFramer::CalculateTimestampFromWire(
    uint32_t time_delta_us) {
  // The new time_delta might have wrapped to the next epoch, or it
  // might have reverse wrapped to the previous epoch, or it might
  // remain in the same epoch. Select the time closest to the previous
  // time.
  //
  // epoch_delta is the delta between epochs. A delta is 4 bytes of
  // microseconds.
  const uint64_t epoch_delta = UINT64_C(1) << 32;
  uint64_t epoch = last_timestamp_.ToMicroseconds() & ~(epoch_delta - 1);
  // Wrapping is safe here because a wrapped value will not be ClosestTo below.
  uint64_t prev_epoch = epoch - epoch_delta;
  uint64_t next_epoch = epoch + epoch_delta;

  uint64_t time = ClosestTo(
      last_timestamp_.ToMicroseconds(), epoch + time_delta_us,
      ClosestTo(last_timestamp_.ToMicroseconds(), prev_epoch + time_delta_us,
                next_epoch + time_delta_us));

  return QuicTime::Delta::FromMicroseconds(time);
}

uint64_t QuicFramer::CalculatePacketNumberFromWire(
    QuicPacketNumberLength packet_number_length,
    QuicPacketNumber base_packet_number, uint64_t packet_number) {
  // The new packet number might have wrapped to the next epoch, or
  // it might have reverse wrapped to the previous epoch, or it might
  // remain in the same epoch.  Select the packet number closest to the
  // next expected packet number, the previous packet number plus 1.

  // epoch_delta is the delta between epochs the packet number was serialized
  // with, so the correct value is likely the same epoch as the last sequence
  // number or an adjacent epoch.
  if (!base_packet_number.IsInitialized()) {
    return packet_number;
  }
  const uint64_t epoch_delta = UINT64_C(1) << (8 * packet_number_length);
  uint64_t next_packet_number = base_packet_number.ToUint64() + 1;
  uint64_t epoch = base_packet_number.ToUint64() & ~(epoch_delta - 1);
  uint64_t prev_epoch = epoch - epoch_delta;
  uint64_t next_epoch = epoch + epoch_delta;

  return ClosestTo(next_packet_number, epoch + packet_number,
                   ClosestTo(next_packet_number, prev_epoch + packet_number,
                             next_epoch + packet_number));
}

// static
QuicPacketNumberLength QuicFramer::GetMinPacketNumberLength(
    QuicPacketNumber packet_number) {
  QUICHE_DCHECK(packet_number.IsInitialized());
  if (packet_number < QuicPacketNumber(1 << (PACKET_1BYTE_PACKET_NUMBER * 8))) {
    return PACKET_1BYTE_PACKET_NUMBER;
  } else if (packet_number <
             QuicPacketNumber(1 << (PACKET_2BYTE_PACKET_NUMBER * 8))) {
    return PACKET_2BYTE_PACKET_NUMBER;
  } else if (packet_number <
             QuicPacketNumber(UINT64_C(1)
                              << (PACKET_4BYTE_PACKET_NUMBER * 8))) {
    return PACKET_4BYTE_PACKET_NUMBER;
  } else {
    return PACKET_6BYTE_PACKET_NUMBER;
  }
}

// static
uint8_t QuicFramer::GetPacketNumberFlags(
    QuicPacketNumberLength packet_number_length) {
  switch (packet_number_length) {
    case PACKET_1BYTE_PACKET_NUMBER:
      return PACKET_FLAGS_1BYTE_PACKET;
    case PACKET_2BYTE_PACKET_NUMBER:
      return PACKET_FLAGS_2BYTE_PACKET;
    case PACKET_4BYTE_PACKET_NUMBER:
      return PACKET_FLAGS_4BYTE_PACKET;
    case PACKET_6BYTE_PACKET_NUMBER:
    case PACKET_8BYTE_PACKET_NUMBER:
      return PACKET_FLAGS_8BYTE_PACKET;
    default:
      QUIC_BUG(quic_bug_10850_56) << "Unreachable case statement.";
      return PACKET_FLAGS_8BYTE_PACKET;
  }
}

// static
QuicFramer::AckFrameInfo QuicFramer::GetAckFrameInfo(
    const QuicAckFrame& frame) {
  AckFrameInfo new_ack_info;
  if (frame.packets.Empty()) {
    return new_ack_info;
  }
  // The first block is the last interval. It isn't encoded with the gap-length
  // encoding, so skip it.
  new_ack_info.first_block_length = frame.packets.LastIntervalLength();
  auto itr = frame.packets.rbegin();
  QuicPacketNumber previous_start = itr->min();
  new_ack_info.max_block_length = itr->Length();
  ++itr;

  // Don't do any more work after getting information for 256 ACK blocks; any
  // more can't be encoded anyway.
  for (; itr != frame.packets.rend() &&
         new_ack_info.num_ack_blocks < std::numeric_limits<uint8_t>::max();
       previous_start = itr->min(), ++itr) {
    const auto& interval = *itr;
    const QuicPacketCount total_gap = previous_start - interval.max();
    new_ack_info.num_ack_blocks +=
        (total_gap + std::numeric_limits<uint8_t>::max() - 1) /
        std::numeric_limits<uint8_t>::max();
    new_ack_info.max_block_length =
        std::max(new_ack_info.max_block_length, interval.Length());
  }
  return new_ack_info;
}

bool QuicFramer::ProcessIetfHeaderTypeByte(QuicDataReader* reader,
                                           QuicPacketHeader* header) {
  uint8_t type;
  if (!reader->ReadBytes(&type, 1)) {
    set_detailed_error("Unable to read first byte.");
    return false;
  }
  header->type_byte = type;
  // Determine whether this is a long or short header.
  header->form = GetIetfPacketHeaderFormat(type);
  if (header->form == IETF_QUIC_LONG_HEADER_PACKET) {
    // Version is always present in long headers.
    header->version_flag = true;
    // In versions that do not support client connection IDs, we mark the
    // corresponding connection ID as absent.
    header->destination_connection_id_included =
        (perspective_ == Perspective::IS_SERVER ||
         version_.SupportsClientConnectionIds())
            ? CONNECTION_ID_PRESENT
            : CONNECTION_ID_ABSENT;
    header->source_connection_id_included =
        (perspective_ == Perspective::IS_CLIENT ||
         version_.SupportsClientConnectionIds())
            ? CONNECTION_ID_PRESENT
            : CONNECTION_ID_ABSENT;
    // Read version tag.
    QuicVersionLabel version_label;
    if (!ProcessVersionLabel(reader, &version_label)) {
      set_detailed_error("Unable to read protocol version.");
      return false;
    }
    if (!version_label) {
      // Version label is 0 indicating this is a version negotiation packet.
      header->long_packet_type = VERSION_NEGOTIATION;
    } else {
      header->version = ParseQuicVersionLabel(version_label);
      if (header->version.IsKnown()) {
        if (!(type & FLAGS_FIXED_BIT)) {
          set_detailed_error("Fixed bit is 0 in long header.");
          return false;
        }
        header->long_packet_type = GetLongHeaderType(type, header->version);
        switch (header->long_packet_type) {
          case INVALID_PACKET_TYPE:
            set_detailed_error("Illegal long header type value.");
            return false;
          case RETRY:
            if (!version().SupportsRetry()) {
              set_detailed_error("RETRY not supported in this version.");
              return false;
            }
            if (perspective_ == Perspective::IS_SERVER) {
              set_detailed_error("Client-initiated RETRY is invalid.");
              return false;
            }
            break;
          default:
            if (!header->version.HasHeaderProtection()) {
              header->packet_number_length =
                  GetLongHeaderPacketNumberLength(type);
            }
            break;
        }
      }
    }

    QUIC_DVLOG(1) << ENDPOINT << "Received IETF long header: "
                  << QuicUtils::QuicLongHeaderTypetoString(
                         header->long_packet_type);
    return true;
  }

  QUIC_DVLOG(1) << ENDPOINT << "Received IETF short header";
  // Version is not present in short headers.
  header->version_flag = false;
  // In versions that do not support client connection IDs, the client will not
  // receive destination connection IDs.
  header->destination_connection_id_included =
      (perspective_ == Perspective::IS_SERVER ||
       version_.SupportsClientConnectionIds())
          ? CONNECTION_ID_PRESENT
          : CONNECTION_ID_ABSENT;
  header->source_connection_id_included = CONNECTION_ID_ABSENT;
  if (!(type & FLAGS_FIXED_BIT)) {
    set_detailed_error("Fixed bit is 0 in short header.");
    return false;
  }
  if (!version_.HasHeaderProtection()) {
    header->packet_number_length = GetShortHeaderPacketNumberLength(type);
  }
  QUIC_DVLOG(1) << "packet_number_length = " << header->packet_number_length;
  return true;
}

// static
bool QuicFramer::ProcessVersionLabel(QuicDataReader* reader,
                                     QuicVersionLabel* version_label) {
  if (!reader->ReadUInt32(version_label)) {
    return false;
  }
  return true;
}

// static
bool QuicFramer::ProcessAndValidateIetfConnectionIdLength(
    QuicDataReader* reader, ParsedQuicVersion version, Perspective perspective,
    bool should_update_expected_server_connection_id_length,
    uint8_t* expected_server_connection_id_length,
    uint8_t* destination_connection_id_length,
    uint8_t* source_connection_id_length, std::string* detailed_error) {
  uint8_t connection_id_lengths_byte;
  if (!reader->ReadBytes(&connection_id_lengths_byte, 1)) {
    *detailed_error = "Unable to read ConnectionId length.";
    return false;
  }
  uint8_t dcil =
      (connection_id_lengths_byte & kDestinationConnectionIdLengthMask) >> 4;
  if (dcil != 0) {
    dcil += kConnectionIdLengthAdjustment;
  }
  uint8_t scil = connection_id_lengths_byte & kSourceConnectionIdLengthMask;
  if (scil != 0) {
    scil += kConnectionIdLengthAdjustment;
  }
  if (should_update_expected_server_connection_id_length) {
    uint8_t server_connection_id_length =
        perspective == Perspective::IS_SERVER ? dcil : scil;
    if (*expected_server_connection_id_length != server_connection_id_length) {
      QUIC_DVLOG(1) << "Updating expected_server_connection_id_length: "
                    << static_cast<int>(*expected_server_connection_id_length)
                    << " -> " << static_cast<int>(server_connection_id_length);
      *expected_server_connection_id_length = server_connection_id_length;
    }
  }
  if (!should_update_expected_server_connection_id_length &&
      (dcil != *destination_connection_id_length ||
       scil != *source_connection_id_length) &&
      version.IsKnown() && !version.AllowsVariableLengthConnectionIds()) {
    QUIC_DVLOG(1) << "dcil: " << static_cast<uint32_t>(dcil)
                  << ", scil: " << static_cast<uint32_t>(scil);
    *detailed_error = "Invalid ConnectionId length.";
    return false;
  }
  *destination_connection_id_length = dcil;
  *source_connection_id_length = scil;
  return true;
}

bool QuicFramer::ValidateReceivedConnectionIds(const QuicPacketHeader& header) {
  bool skip_server_connection_id_validation =
      perspective_ == Perspective::IS_CLIENT &&
      header.form == IETF_QUIC_SHORT_HEADER_PACKET;
  if (!skip_server_connection_id_validation &&
      !QuicUtils::IsConnectionIdValidForVersion(
          GetServerConnectionIdAsRecipient(header, perspective_),
          transport_version())) {
    set_detailed_error("Received server connection ID with invalid length.");
    return false;
  }

  bool skip_client_connection_id_validation =
      perspective_ == Perspective::IS_SERVER &&
      header.form == IETF_QUIC_SHORT_HEADER_PACKET;
  if (!skip_client_connection_id_validation &&
      version_.SupportsClientConnectionIds() &&
      !QuicUtils::IsConnectionIdValidForVersion(
          GetClientConnectionIdAsRecipient(header, perspective_),
          transport_version())) {
    set_detailed_error("Received client connection ID with invalid length.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessIetfPacketHeader(QuicDataReader* reader,
                                         QuicPacketHeader* header) {
  if (version_.HasLengthPrefixedConnectionIds()) {
    uint8_t expected_destination_connection_id_length =
        perspective_ == Perspective::IS_CLIENT
            ? expected_client_connection_id_length_
            : expected_server_connection_id_length_;
    QuicVersionLabel version_label;
    bool has_length_prefix;
    std::string detailed_error;
    QuicErrorCode parse_result = QuicFramer::ParsePublicHeader(
        reader, expected_destination_connection_id_length, /*ietf_format=*/true,
        &header->type_byte, &header->form, &header->version_flag,
        &has_length_prefix, &version_label, &header->version,
        &header->destination_connection_id, &header->source_connection_id,
        &header->long_packet_type, &header->retry_token_length_length,
        &header->retry_token, &detailed_error);
    if (parse_result != QUIC_NO_ERROR) {
      set_detailed_error(detailed_error);
      return false;
    }
    header->destination_connection_id_included = CONNECTION_ID_PRESENT;
    header->source_connection_id_included =
        header->version_flag ? CONNECTION_ID_PRESENT : CONNECTION_ID_ABSENT;

    if (!ValidateReceivedConnectionIds(*header)) {
      return false;
    }

    if (header->version_flag &&
        header->long_packet_type != VERSION_NEGOTIATION &&
        !(header->type_byte & FLAGS_FIXED_BIT)) {
      set_detailed_error("Fixed bit is 0 in long header.");
      return false;
    }
    if (!header->version_flag && !(header->type_byte & FLAGS_FIXED_BIT)) {
      set_detailed_error("Fixed bit is 0 in short header.");
      return false;
    }
    if (!header->version_flag) {
      if (!version_.HasHeaderProtection()) {
        header->packet_number_length =
            GetShortHeaderPacketNumberLength(header->type_byte);
      }
      return true;
    }
    if (header->long_packet_type == RETRY) {
      if (!version().SupportsRetry()) {
        set_detailed_error("RETRY not supported in this version.");
        return false;
      }
      if (perspective_ == Perspective::IS_SERVER) {
        set_detailed_error("Client-initiated RETRY is invalid.");
        return false;
      }
      return true;
    }
    if (header->version.IsKnown() && !header->version.HasHeaderProtection()) {
      header->packet_number_length =
          GetLongHeaderPacketNumberLength(header->type_byte);
    }

    return true;
  }

  if (!ProcessIetfHeaderTypeByte(reader, header)) {
    return false;
  }

  uint8_t destination_connection_id_length =
      header->destination_connection_id_included == CONNECTION_ID_PRESENT
          ? (perspective_ == Perspective::IS_SERVER
                 ? expected_server_connection_id_length_
                 : expected_client_connection_id_length_)
          : 0;
  uint8_t source_connection_id_length =
      header->source_connection_id_included == CONNECTION_ID_PRESENT
          ? (perspective_ == Perspective::IS_CLIENT
                 ? expected_server_connection_id_length_
                 : expected_client_connection_id_length_)
          : 0;
  if (header->form == IETF_QUIC_LONG_HEADER_PACKET) {
    if (!ProcessAndValidateIetfConnectionIdLength(
            reader, header->version, perspective_,
            /*should_update_expected_server_connection_id_length=*/false,
            &expected_server_connection_id_length_,
            &destination_connection_id_length, &source_connection_id_length,
            &detailed_error_)) {
      return false;
    }
  }

  // Read connection ID.
  if (!reader->ReadConnectionId(&header->destination_connection_id,
                                destination_connection_id_length)) {
    set_detailed_error("Unable to read destination connection ID.");
    return false;
  }

  if (!reader->ReadConnectionId(&header->source_connection_id,
                                source_connection_id_length)) {
    set_detailed_error("Unable to read source connection ID.");
    return false;
  }

  if (header->source_connection_id_included == CONNECTION_ID_ABSENT) {
    if (!header->source_connection_id.IsEmpty()) {
      QUICHE_DCHECK(!version_.SupportsClientConnectionIds());
      set_detailed_error("Client connection ID not supported in this version.");
      return false;
    }
  }

  return ValidateReceivedConnectionIds(*header);
}

bool QuicFramer::ProcessAndCalculatePacketNumber(
    QuicDataReader* reader, QuicPacketNumberLength packet_number_length,
    QuicPacketNumber base_packet_number, uint64_t* packet_number) {
  uint64_t wire_packet_number;
  if (!reader->ReadBytesToUInt64(packet_number_length, &wire_packet_number)) {
    return false;
  }

  // TODO(ianswett): Explore the usefulness of trying multiple packet numbers
  // in case the first guess is incorrect.
  *packet_number = CalculatePacketNumberFromWire(
      packet_number_length, base_packet_number, wire_packet_number);
  return true;
}

bool QuicFramer::ProcessFrameData(QuicDataReader* reader,
                                  const QuicPacketHeader& header) {
  QUICHE_DCHECK(!VersionHasIetfQuicFrames(version_.transport_version))
      << "IETF QUIC Framing negotiated but attempting to process frames as "
         "non-IETF QUIC.";
  if (reader->IsDoneReading()) {
    set_detailed_error("Packet has no frames.");
    return RaiseError(QUIC_MISSING_PAYLOAD);
  }
  QUIC_DVLOG(2) << ENDPOINT << "Processing packet with header " << header;
  while (!reader->IsDoneReading()) {
    uint8_t frame_type;
    if (!reader->ReadBytes(&frame_type, 1)) {
      set_detailed_error("Unable to read frame type.");
      return RaiseError(QUIC_INVALID_FRAME_DATA);
    }
    if (frame_type & kQuicFrameTypeSpecialMask) {
      // Stream Frame
      if (frame_type & kQuicFrameTypeStreamMask) {
        QuicStreamFrame frame;
        if (!ProcessStreamFrame(reader, frame_type, &frame)) {
          return RaiseError(QUIC_INVALID_STREAM_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing stream frame " << frame;
        if (!visitor_->OnStreamFrame(frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      // Ack Frame
      if (frame_type & kQuicFrameTypeAckMask) {
        if (!ProcessAckFrame(reader, frame_type)) {
          return RaiseError(QUIC_INVALID_ACK_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing ACK frame";
        continue;
      }

      // This was a special frame type that did not match any
      // of the known ones. Error.
      set_detailed_error("Illegal frame type.");
      QUIC_DLOG(WARNING) << ENDPOINT << "Illegal frame type: "
                         << static_cast<int>(frame_type);
      return RaiseError(QUIC_INVALID_FRAME_DATA);
    }

    switch (frame_type) {
      case PADDING_FRAME: {
        QuicPaddingFrame frame;
        ProcessPaddingFrame(reader, &frame);
        QUIC_DVLOG(2) << ENDPOINT << "Processing padding frame " << frame;
        if (!visitor_->OnPaddingFrame(frame)) {
          QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      case RST_STREAM_FRAME: {
        QuicRstStreamFrame frame;
        if (!ProcessRstStreamFrame(reader, &frame)) {
          return RaiseError(QUIC_INVALID_RST_STREAM_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing reset stream frame " << frame;
        if (!visitor_->OnRstStreamFrame(frame)) {
          QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      case CONNECTION_CLOSE_FRAME: {
        QuicConnectionCloseFrame frame;
        if (!ProcessConnectionCloseFrame(reader, &frame)) {
          return RaiseError(QUIC_INVALID_CONNECTION_CLOSE_DATA);
        }

        QUIC_DVLOG(2) << ENDPOINT << "Processing connection close frame "
                      << frame;
        if (!visitor_->OnConnectionCloseFrame(frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      case GOAWAY_FRAME: {
        QuicGoAwayFrame goaway_frame;
        if (!ProcessGoAwayFrame(reader, &goaway_frame)) {
          return RaiseError(QUIC_INVALID_GOAWAY_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing go away frame "
                      << goaway_frame;
        if (!visitor_->OnGoAwayFrame(goaway_frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      case WINDOW_UPDATE_FRAME: {
        QuicWindowUpdateFrame window_update_frame;
        if (!ProcessWindowUpdateFrame(reader, &window_update_frame)) {
          return RaiseError(QUIC_INVALID_WINDOW_UPDATE_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing window update frame "
                      << window_update_frame;
        if (!visitor_->OnWindowUpdateFrame(window_update_frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      case BLOCKED_FRAME: {
        QuicBlockedFrame blocked_frame;
        if (!ProcessBlockedFrame(reader, &blocked_frame)) {
          return RaiseError(QUIC_INVALID_BLOCKED_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing blocked frame "
                      << blocked_frame;
        if (!visitor_->OnBlockedFrame(blocked_frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }

      case STOP_WAITING_FRAME: {
        QuicStopWaitingFrame stop_waiting_frame;
        if (!ProcessStopWaitingFrame(reader, header, &stop_waiting_frame)) {
          return RaiseError(QUIC_INVALID_STOP_WAITING_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing stop waiting frame "
                      << stop_waiting_frame;
        if (!visitor_->OnStopWaitingFrame(stop_waiting_frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        continue;
      }
      case PING_FRAME: {
        // Ping has no payload.
        QuicPingFrame ping_frame;
        if (!visitor_->OnPingFrame(ping_frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing ping frame " << ping_frame;
        continue;
      }
      case IETF_EXTENSION_MESSAGE_NO_LENGTH:
        ABSL_FALLTHROUGH_INTENDED;
      case IETF_EXTENSION_MESSAGE: {
        QUIC_CODE_COUNT(quic_legacy_message_frame_codepoint_read);
        QuicMessageFrame message_frame;
        if (!ProcessMessageFrame(reader,
                                 frame_type == IETF_EXTENSION_MESSAGE_NO_LENGTH,
                                 &message_frame)) {
          return RaiseError(QUIC_INVALID_MESSAGE_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing message frame "
                      << message_frame;
        if (!visitor_->OnMessageFrame(message_frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        break;
      }
      case CRYPTO_FRAME: {
        if (!QuicVersionUsesCryptoFrames(version_.transport_version)) {
          set_detailed_error("Illegal frame type.");
          return RaiseError(QUIC_INVALID_FRAME_DATA);
        }
        QuicCryptoFrame frame;
        if (!ProcessCryptoFrame(reader, GetEncryptionLevel(header), &frame)) {
          return RaiseError(QUIC_INVALID_FRAME_DATA);
        }
        QUIC_DVLOG(2) << ENDPOINT << "Processing crypto frame " << frame;
        if (!visitor_->OnCryptoFrame(frame)) {
          QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        break;
      }
      case HANDSHAKE_DONE_FRAME: {
        // HANDSHAKE_DONE has no payload.
        QuicHandshakeDoneFrame handshake_done_frame;
        QUIC_DVLOG(2) << ENDPOINT << "Processing handshake done frame "
                      << handshake_done_frame;
        if (!visitor_->OnHandshakeDoneFrame(handshake_done_frame)) {
          QUIC_DVLOG(1) << ENDPOINT
                        << "Visitor asked to stop further processing.";
          // Returning true since there was no parsing error.
          return true;
        }
        break;
      }

      default:
        set_detailed_error("Illegal frame type.");
        QUIC_DLOG(WARNING) << ENDPOINT << "Illegal frame type: "
                           << static_cast<int>(frame_type);
        return RaiseError(QUIC_INVALID_FRAME_DATA);
    }
  }

  return true;
}

// static
bool QuicFramer::IsIetfFrameTypeExpectedForEncryptionLevel(
    uint64_t frame_type, EncryptionLevel level) {
  // IETF_CRYPTO is allowed for any level here and is separately checked in
  // QuicCryptoStream::OnCryptoFrame.
  switch (level) {
    case ENCRYPTION_INITIAL:
    case ENCRYPTION_HANDSHAKE:
      return frame_type == IETF_CRYPTO || frame_type == IETF_ACK ||
             frame_type == IETF_ACK_ECN ||
             frame_type == IETF_ACK_RECEIVE_TIMESTAMPS ||
             frame_type == IETF_PING || frame_type == IETF_PADDING ||
             frame_type == IETF_CONNECTION_CLOSE;
    case ENCRYPTION_ZERO_RTT:
      return !(frame_type == IETF_ACK || frame_type == IETF_ACK_ECN ||
               frame_type == IETF_ACK_RECEIVE_TIMESTAMPS ||
               frame_type == IETF_HANDSHAKE_DONE ||
               frame_type == IETF_NEW_TOKEN ||
               frame_type == IETF_PATH_RESPONSE ||
               frame_type == IETF_RETIRE_CONNECTION_ID);
    case ENCRYPTION_FORWARD_SECURE:
      return true;
    default:
      QUIC_BUG(quic_bug_10850_57) << "Unknown encryption level: " << level;
  }
  return false;
}

bool QuicFramer::ProcessIetfFrameData(QuicDataReader* reader,
                                      const QuicPacketHeader& header,
                                      EncryptionLevel decrypted_level) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(version_.transport_version))
      << "Attempt to process frames as IETF frames but version ("
      << version_.transport_version << ") does not support IETF Framing.";

  if (reader->IsDoneReading()) {
    set_detailed_error("Packet has no frames.");
    return RaiseError(QUIC_MISSING_PAYLOAD);
  }

  QUIC_DVLOG(2) << ENDPOINT << "Processing IETF packet with header " << header;
  while (!reader->IsDoneReading()) {
    uint64_t frame_type;
    // Will be the number of bytes into which frame_type was encoded.
    size_t encoded_bytes = reader->BytesRemaining();
    if (!reader->ReadVarInt62(&frame_type)) {
      set_detailed_error("Unable to read frame type.");
      return RaiseError(QUIC_INVALID_FRAME_DATA);
    }
    if (!IsIetfFrameTypeExpectedForEncryptionLevel(frame_type,
                                                   decrypted_level)) {
      set_detailed_error(absl::StrCat(
          "IETF frame type ",
          QuicIetfFrameTypeString(static_cast<QuicIetfFrameType>(frame_type)),
          " is unexpected at encryption level ",
          EncryptionLevelToString(decrypted_level)));
      return RaiseError(IETF_QUIC_PROTOCOL_VIOLATION);
    }
    previously_received_frame_type_ = current_received_frame_type_;
    current_received_frame_type_ = frame_type;

    // Is now the number of bytes into which the frame type was encoded.
    encoded_bytes -= reader->BytesRemaining();

    // Check that the frame type is minimally encoded.
    if (encoded_bytes !=
        static_cast<size_t>(QuicDataWriter::GetVarInt62Len(frame_type))) {
      // The frame type was not minimally encoded.
      set_detailed_error("Frame type not minimally encoded.");
      return RaiseError(IETF_QUIC_PROTOCOL_VIOLATION);
    }

    if (IS_IETF_STREAM_FRAME(frame_type)) {
      QuicStreamFrame frame;
      if (!ProcessIetfStreamFrame(reader, frame_type, &frame)) {
        return RaiseError(QUIC_INVALID_STREAM_DATA);
      }
      QUIC_DVLOG(2) << ENDPOINT << "Processing IETF stream frame " << frame;
      if (!visitor_->OnStreamFrame(frame)) {
        QUIC_DVLOG(1) << ENDPOINT
                      << "Visitor asked to stop further processing.";
        // Returning true since there was no parsing error.
        return true;
      }
    } else {
      switch (frame_type) {
        case IETF_PADDING: {
          QuicPaddingFrame frame;
          ProcessPaddingFrame(reader, &frame);
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF padding frame "
                        << frame;
          if (!visitor_->OnPaddingFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_RST_STREAM: {
          QuicRstStreamFrame frame;
          if (!ProcessIetfResetStreamFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_RST_STREAM_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF reset stream frame "
                        << frame;
          if (!visitor_->OnRstStreamFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_APPLICATION_CLOSE:
        case IETF_CONNECTION_CLOSE: {
          QuicConnectionCloseFrame frame;
          if (!ProcessIetfConnectionCloseFrame(
                  reader,
                  (frame_type == IETF_CONNECTION_CLOSE)
                      ? IETF_QUIC_TRANSPORT_CONNECTION_CLOSE
                      : IETF_QUIC_APPLICATION_CONNECTION_CLOSE,
                  &frame)) {
            return RaiseError(QUIC_INVALID_CONNECTION_CLOSE_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF connection close frame "
                        << frame;
          if (!visitor_->OnConnectionCloseFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_MAX_DATA: {
          QuicWindowUpdateFrame frame;
          if (!ProcessMaxDataFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_MAX_DATA_FRAME_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF max data frame "
                        << frame;
          if (!visitor_->OnWindowUpdateFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_MAX_STREAM_DATA: {
          QuicWindowUpdateFrame frame;
          if (!ProcessMaxStreamDataFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_MAX_STREAM_DATA_FRAME_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF max stream data frame "
                        << frame;
          if (!visitor_->OnWindowUpdateFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_MAX_STREAMS_BIDIRECTIONAL:
        case IETF_MAX_STREAMS_UNIDIRECTIONAL: {
          QuicMaxStreamsFrame frame;
          if (!ProcessMaxStreamsFrame(reader, &frame, frame_type)) {
            return RaiseError(QUIC_MAX_STREAMS_DATA);
          }
          QUIC_CODE_COUNT_N(quic_max_streams_received, 1, 2);
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF max streams frame "
                        << frame;
          if (!visitor_->OnMaxStreamsFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_PING: {
          // Ping has no payload.
          QuicPingFrame ping_frame;
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF ping frame "
                        << ping_frame;
          if (!visitor_->OnPingFrame(ping_frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_DATA_BLOCKED: {
          QuicBlockedFrame frame;
          if (!ProcessDataBlockedFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_BLOCKED_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF blocked frame "
                        << frame;
          if (!visitor_->OnBlockedFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_STREAM_DATA_BLOCKED: {
          QuicBlockedFrame frame;
          if (!ProcessStreamDataBlockedFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_STREAM_BLOCKED_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF stream blocked frame "
                        << frame;
          if (!visitor_->OnBlockedFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_STREAMS_BLOCKED_UNIDIRECTIONAL:
        case IETF_STREAMS_BLOCKED_BIDIRECTIONAL: {
          QuicStreamsBlockedFrame frame;
          if (!ProcessStreamsBlockedFrame(reader, &frame, frame_type)) {
            return RaiseError(QUIC_STREAMS_BLOCKED_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF streams blocked frame "
                        << frame;
          if (!visitor_->OnStreamsBlockedFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_NEW_CONNECTION_ID: {
          QuicNewConnectionIdFrame frame;
          if (!ProcessNewConnectionIdFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_NEW_CONNECTION_ID_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT
                        << "Processing IETF new connection ID frame " << frame;
          if (!visitor_->OnNewConnectionIdFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_RETIRE_CONNECTION_ID: {
          QuicRetireConnectionIdFrame frame;
          if (!ProcessRetireConnectionIdFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_RETIRE_CONNECTION_ID_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT
                        << "Processing IETF retire connection ID frame "
                        << frame;
          if (!visitor_->OnRetireConnectionIdFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_NEW_TOKEN: {
          QuicNewTokenFrame frame;
          if (!ProcessNewTokenFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_NEW_TOKEN);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF new token frame "
                        << frame;
          if (!visitor_->OnNewTokenFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_STOP_SENDING: {
          QuicStopSendingFrame frame;
          if (!ProcessStopSendingFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_STOP_SENDING_FRAME_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF stop sending frame "
                        << frame;
          if (!visitor_->OnStopSendingFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_ACK_RECEIVE_TIMESTAMPS:
          if (!process_timestamps_) {
            set_detailed_error("Unsupported frame type.");
            QUIC_DLOG(WARNING)
                << ENDPOINT << "IETF_ACK_RECEIVE_TIMESTAMPS not supported";
            return RaiseError(QUIC_INVALID_FRAME_DATA);
          }
          ABSL_FALLTHROUGH_INTENDED;
        case IETF_ACK_ECN:
        case IETF_ACK: {
          QuicAckFrame frame;
          if (!ProcessIetfAckFrame(reader, frame_type, &frame)) {
            return RaiseError(QUIC_INVALID_ACK_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF ACK frame " << frame;
          break;
        }
        case IETF_PATH_CHALLENGE: {
          QuicPathChallengeFrame frame;
          if (!ProcessPathChallengeFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_PATH_CHALLENGE_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF path challenge frame "
                        << frame;
          if (!visitor_->OnPathChallengeFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_PATH_RESPONSE: {
          QuicPathResponseFrame frame;
          if (!ProcessPathResponseFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_PATH_RESPONSE_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF path response frame "
                        << frame;
          if (!visitor_->OnPathResponseFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_EXTENSION_MESSAGE_NO_LENGTH_V99:
          ABSL_FALLTHROUGH_INTENDED;
        case IETF_EXTENSION_MESSAGE_V99: {
          QuicMessageFrame message_frame;
          if (!ProcessMessageFrame(
                  reader, frame_type == IETF_EXTENSION_MESSAGE_NO_LENGTH_V99,
                  &message_frame)) {
            return RaiseError(QUIC_INVALID_MESSAGE_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF message frame "
                        << message_frame;
          if (!visitor_->OnMessageFrame(message_frame)) {
            QUIC_DVLOG(1) << ENDPOINT
                          << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_CRYPTO: {
          QuicCryptoFrame frame;
          if (!ProcessCryptoFrame(reader, GetEncryptionLevel(header), &frame)) {
            return RaiseError(QUIC_INVALID_FRAME_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF crypto frame " << frame;
          if (!visitor_->OnCryptoFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_HANDSHAKE_DONE: {
          // HANDSHAKE_DONE has no payload.
          QuicHandshakeDoneFrame handshake_done_frame;
          if (!visitor_->OnHandshakeDoneFrame(handshake_done_frame)) {
            QUIC_DVLOG(1) << ENDPOINT
                          << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing handshake done frame "
                        << handshake_done_frame;
          break;
        }
        case IETF_ACK_FREQUENCY: {
          QuicAckFrequencyFrame frame;
          if (!ProcessAckFrequencyFrame(reader, &frame)) {
            return RaiseError(QUIC_INVALID_FRAME_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF ack frequency frame "
                        << frame;
          if (!visitor_->OnAckFrequencyFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_IMMEDIATE_ACK: {
          // IMMEDIATE_ACK has no payload.
          QuicImmediateAckFrame frame;
          QUIC_DVLOG(2) << ENDPOINT << "Processing IETF immediate ack frame "
                        << frame;
          if (!visitor_->OnImmediateAckFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        case IETF_RESET_STREAM_AT: {
          if (!process_reset_stream_at_) {
            set_detailed_error("RESET_STREAM_AT not enabled.");
            return RaiseError(QUIC_INVALID_FRAME_DATA);
          }
          QuicResetStreamAtFrame frame;
          if (!ProcessResetStreamAtFrame(*reader, frame)) {
            return RaiseError(QUIC_INVALID_FRAME_DATA);
          }
          QUIC_DVLOG(2) << ENDPOINT << "Processing RESET_STREAM_AT frame "
                        << frame;
          if (!visitor_->OnResetStreamAtFrame(frame)) {
            QUIC_DVLOG(1) << "Visitor asked to stop further processing.";
            // Returning true since there was no parsing error.
            return true;
          }
          break;
        }
        default:
          set_detailed_error("Illegal frame type.");
          QUIC_DLOG(WARNING)
              << ENDPOINT
              << "Illegal frame type: " << static_cast<int>(frame_type);
          return RaiseError(QUIC_INVALID_FRAME_DATA);
      }
    }
  }
  return true;
}

namespace {
// Create a mask that sets the last |num_bits| to 1 and the rest to 0.
inline uint8_t GetMaskFromNumBits(uint8_t num_bits) {
  return (1u << num_bits) - 1;
}

// Extract |num_bits| from |flags| offset by |offset|.
uint8_t ExtractBits(uint8_t flags, uint8_t num_bits, uint8_t offset) {
  return (flags >> offset) & GetMaskFromNumBits(num_bits);
}

// Extract the bit at position |offset| from |flags| as a bool.
bool ExtractBit(uint8_t flags, uint8_t offset) {
  return ((flags >> offset) & GetMaskFromNumBits(1)) != 0;
}

// Set |num_bits|, offset by |offset| to |val| in |flags|.
void SetBits(uint8_t* flags, uint8_t val, uint8_t num_bits, uint8_t offset) {
  QUICHE_DCHECK_LE(val, GetMaskFromNumBits(num_bits));
  *flags |= val << offset;
}

// Set the bit at position |offset| to |val| in |flags|.
void SetBit(uint8_t* flags, bool val, uint8_t offset) {
  SetBits(flags, val ? 1 : 0, 1, offset);
}
}  // namespace

bool QuicFramer::ProcessStreamFrame(QuicDataReader* reader, uint8_t frame_type,
                                    QuicStreamFrame* frame) {
  uint8_t stream_flags = frame_type;

  uint8_t stream_id_length = 0;
  uint8_t offset_length = 4;
  bool has_data_length = true;
  stream_flags &= ~kQuicFrameTypeStreamMask;

  // Read from right to left: StreamID, Offset, Data Length, Fin.
  stream_id_length = (stream_flags & kQuicStreamIDLengthMask) + 1;
  stream_flags >>= kQuicStreamIdShift;

  offset_length = (stream_flags & kQuicStreamOffsetMask);
  // There is no encoding for 1 byte, only 0 and 2 through 8.
  if (offset_length > 0) {
    offset_length += 1;
  }
  stream_flags >>= kQuicStreamShift;

  has_data_length =
      (stream_flags & kQuicStreamDataLengthMask) == kQuicStreamDataLengthMask;
  stream_flags >>= kQuicStreamDataLengthShift;

  frame->fin = (stream_flags & kQuicStreamFinMask) == kQuicStreamFinShift;

  uint64_t stream_id;
  if (!reader->ReadBytesToUInt64(stream_id_length, &stream_id)) {
    set_detailed_error("Unable to read stream_id.");
    return false;
  }
  frame->stream_id = static_cast<QuicStreamId>(stream_id);

  if (!reader->ReadBytesToUInt64(offset_length, &frame->offset)) {
    set_detailed_error("Unable to read offset.");
    return false;
  }

  // TODO(ianswett): Don't use absl::string_view as an intermediary.
  absl::string_view data;
  if (has_data_length) {
    if (!reader->ReadStringPiece16(&data)) {
      set_detailed_error("Unable to read frame data.");
      return false;
    }
  } else {
    if (!reader->ReadStringPiece(&data, reader->BytesRemaining())) {
      set_detailed_error("Unable to read frame data.");
      return false;
    }
  }
  frame->data_buffer = data.data();
  frame->data_length = static_cast<uint16_t>(data.length());

  return true;
}

bool QuicFramer::ProcessIetfStreamFrame(QuicDataReader* reader,
                                        uint8_t frame_type,
                                        QuicStreamFrame* frame) {
  // Read stream id from the frame. It's always present.
  if (!ReadUint32FromVarint62(reader, IETF_STREAM, &frame->stream_id)) {
    return false;
  }

  // If we have a data offset, read it. If not, set to 0.
  if (frame_type & IETF_STREAM_FRAME_OFF_BIT) {
    if (!reader->ReadVarInt62(&frame->offset)) {
      set_detailed_error("Unable to read stream data offset.");
      return false;
    }
  } else {
    // no offset in the frame, ensure it's 0 in the Frame.
    frame->offset = 0;
  }

  // If we have a data length, read it. If not, set to 0.
  if (frame_type & IETF_STREAM_FRAME_LEN_BIT) {
    uint64_t length;
    if (!reader->ReadVarInt62(&length)) {
      set_detailed_error("Unable to read stream data length.");
      return false;
    }
    if (length > std::numeric_limits<decltype(frame->data_length)>::max()) {
      set_detailed_error("Stream data length is too large.");
      return false;
    }
    frame->data_length = length;
  } else {
    // no length in the frame, it is the number of bytes remaining in the
    // packet.
    frame->data_length = reader->BytesRemaining();
  }

  if (frame_type & IETF_STREAM_FRAME_FIN_BIT) {
    frame->fin = true;
  } else {
    frame->fin = false;
  }

  // TODO(ianswett): Don't use absl::string_view as an intermediary.
  absl::string_view data;
  if (!reader->ReadStringPiece(&data, frame->data_length)) {
    set_detailed_error("Unable to read frame data.");
    return false;
  }
  frame->data_buffer = data.data();
  QUICHE_DCHECK_EQ(frame->data_length, data.length());

  return true;
}

bool QuicFramer::ProcessCryptoFrame(QuicDataReader* reader,
                                    EncryptionLevel encryption_level,
                                    QuicCryptoFrame* frame) {
  frame->level = encryption_level;
  if (!reader->ReadVarInt62(&frame->offset)) {
    set_detailed_error("Unable to read crypto data offset.");
    return false;
  }
  uint64_t len;
  if (!reader->ReadVarInt62(&len) ||
      len > std::numeric_limits<QuicPacketLength>::max()) {
    set_detailed_error("Invalid data length.");
    return false;
  }
  frame->data_length = len;

  // TODO(ianswett): Don't use absl::string_view as an intermediary.
  absl::string_view data;
  if (!reader->ReadStringPiece(&data, frame->data_length)) {
    set_detailed_error("Unable to read frame data.");
    return false;
  }
  frame->data_buffer = data.data();
  return true;
}

bool QuicFramer::ProcessAckFrequencyFrame(QuicDataReader* reader,
                                          QuicAckFrequencyFrame* frame) {
  if (!reader->ReadVarInt62(&frame->sequence_number)) {
    set_detailed_error("Unable to read sequence number.");
    return false;
  }

  if (!reader->ReadVarInt62(&frame->packet_tolerance)) {
    set_detailed_error("Unable to read packet tolerance.");
    return false;
  }
  if (frame->packet_tolerance == 0) {
    set_detailed_error("Invalid packet tolerance.");
    return false;
  }
  uint64_t max_ack_delay_us;
  if (!reader->ReadVarInt62(&max_ack_delay_us)) {
    set_detailed_error("Unable to read max_ack_delay_us.");
    return false;
  }
  constexpr uint64_t kMaxAckDelayUsBound = 1u << 24;
  if (max_ack_delay_us > kMaxAckDelayUsBound) {
    set_detailed_error("Invalid max_ack_delay_us.");
    return false;
  }
  frame->max_ack_delay = QuicTime::Delta::FromMicroseconds(max_ack_delay_us);

  uint8_t ignore_order;
  if (!reader->ReadUInt8(&ignore_order)) {
    set_detailed_error("Unable to read ignore_order.");
    return false;
  }
  if (ignore_order > 1) {
    set_detailed_error("Invalid ignore_order.");
    return false;
  }
  frame->ignore_order = ignore_order;

  return true;
}

bool QuicFramer::ProcessResetStreamAtFrame(QuicDataReader& reader,
                                           QuicResetStreamAtFrame& frame) {
  if (!ReadUint32FromVarint62(&reader, IETF_RESET_STREAM_AT,
                              &frame.stream_id)) {
    return false;
  }
  if (!reader.ReadVarInt62(&frame.error)) {
    set_detailed_error("Failed to read the error code.");
    return false;
  }
  if (!reader.ReadVarInt62(&frame.final_offset)) {
    set_detailed_error("Failed to read the final offset.");
    return false;
  }
  if (!reader.ReadVarInt62(&frame.reliable_offset)) {
    set_detailed_error("Failed to read the reliable offset.");
    return false;
  }
  if (frame.reliable_offset > frame.final_offset) {
    set_detailed_error("reliable_offset > final_offset");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessAckFrame(QuicDataReader* reader, uint8_t frame_type) {
  const bool has_ack_blocks =
      ExtractBit(frame_type, kQuicHasMultipleAckBlocksOffset);
  uint8_t num_ack_blocks = 0;
  uint8_t num_received_packets = 0;

  // Determine the two lengths from the frame type: largest acked length,
  // ack block length.
  const QuicPacketNumberLength ack_block_length =
      ReadAckPacketNumberLength(ExtractBits(
          frame_type, kQuicSequenceNumberLengthNumBits, kActBlockLengthOffset));
  const QuicPacketNumberLength largest_acked_length =
      ReadAckPacketNumberLength(ExtractBits(
          frame_type, kQuicSequenceNumberLengthNumBits, kLargestAckedOffset));

  uint64_t largest_acked;
  if (!reader->ReadBytesToUInt64(largest_acked_length, &largest_acked)) {
    set_detailed_error("Unable to read largest acked.");
    return false;
  }

  if (largest_acked < first_sending_packet_number_.ToUint64()) {
    // Connection always sends packet starting from kFirstSendingPacketNumber >
    // 0, peer has observed an unsent packet.
    set_detailed_error("Largest acked is 0.");
    return false;
  }

  uint64_t ack_delay_time_us;
  if (!reader->ReadUFloat16(&ack_delay_time_us)) {
    set_detailed_error("Unable to read ack delay time.");
    return false;
  }

  if (!visitor_->OnAckFrameStart(
          QuicPacketNumber(largest_acked),
          ack_delay_time_us == kUFloat16MaxValue
              ? QuicTime::Delta::Infinite()
              : QuicTime::Delta::FromMicroseconds(ack_delay_time_us))) {
    // The visitor suppresses further processing of the packet. Although this is
    // not a parsing error, returns false as this is in middle of processing an
    // ack frame,
    set_detailed_error("Visitor suppresses further processing of ack frame.");
    return false;
  }

  if (has_ack_blocks && !reader->ReadUInt8(&num_ack_blocks)) {
    set_detailed_error("Unable to read num of ack blocks.");
    return false;
  }

  uint64_t first_block_length;
  if (!reader->ReadBytesToUInt64(ack_block_length, &first_block_length)) {
    set_detailed_error("Unable to read first ack block length.");
    return false;
  }

  if (first_block_length == 0) {
    set_detailed_error("First block length is zero.");
    return false;
  }
  bool first_ack_block_underflow = first_block_length > largest_acked + 1;
  if (first_block_length + first_sending_packet_number_.ToUint64() >
      largest_acked + 1) {
    first_ack_block_underflow = true;
  }
  if (first_ack_block_underflow) {
    set_detailed_error(absl::StrCat("Underflow with first ack block length ",
                                    first_block_length, " largest acked is ",
                                    largest_acked, ".")
                           .c_str());
    return false;
  }

  uint64_t first_received = largest_acked + 1 - first_block_length;
  if (!visitor_->OnAckRange(QuicPacketNumber(first_received),
                            QuicPacketNumber(largest_acked + 1))) {
    // The visitor suppresses further processing of the packet. Although
    // this is not a parsing error, returns false as this is in middle
    // of processing an ack frame,
    set_detailed_error("Visitor suppresses further processing of ack frame.");
    return false;
  }

  if (num_ack_blocks > 0) {
    for (size_t i = 0; i < num_ack_blocks; ++i) {
      uint8_t gap = 0;
      if (!reader->ReadUInt8(&gap)) {
        set_detailed_error("Unable to read gap to next ack block.");
        return false;
      }
      uint64_t current_block_length;
      if (!reader->ReadBytesToUInt64(ack_block_length, &current_block_length)) {
        set_detailed_error("Unable to ack block length.");
        return false;
      }
      bool ack_block_underflow = first_received < gap + current_block_length;
      if (first_received < gap + current_block_length +
                               first_sending_packet_number_.ToUint64()) {
        ack_block_underflow = true;
      }
      if (ack_block_underflow) {
        set_detailed_error(absl::StrCat("Underflow with ack block length ",
                                        current_block_length,
                                        ", end of block is ",
                                        first_received - gap, ".")
                               .c_str());
        return false;
      }

      first_received -= (gap + current_block_length);
      if (current_block_length > 0) {
        if (!visitor_->OnAckRange(
                QuicPacketNumber(first_received),
                QuicPacketNumber(first_received) + current_block_length)) {
          // The visitor suppresses further processing of the packet. Although
          // this is not a parsing error, returns false as this is in middle
          // of processing an ack frame,
          set_detailed_error(
              "Visitor suppresses further processing of ack frame.");
          return false;
        }
      }
    }
  }

  if (!reader->ReadUInt8(&num_received_packets)) {
    set_detailed_error("Unable to read num received packets.");
    return false;
  }

  if (!ProcessTimestampsInAckFrame(num_received_packets,
                                   QuicPacketNumber(largest_acked), reader)) {
    return false;
  }

  // Done processing the ACK frame.
  std::optional<QuicEcnCounts> ecn_counts = std::nullopt;
  if (!visitor_->OnAckFrameEnd(QuicPacketNumber(first_received), ecn_counts)) {
    set_detailed_error(
        "Error occurs when visitor finishes processing the ACK frame.");
    return false;
  }

  return true;
}

bool QuicFramer::ProcessTimestampsInAckFrame(uint8_t num_received_packets,
                                             QuicPacketNumber largest_acked,
                                             QuicDataReader* reader) {
  if (num_received_packets == 0) {
    return true;
  }
  uint8_t delta_from_largest_observed;
  if (!reader->ReadUInt8(&delta_from_largest_observed)) {
    set_detailed_error("Unable to read sequence delta in received packets.");
    return false;
  }

  if (largest_acked.ToUint64() <= delta_from_largest_observed) {
    set_detailed_error(
        absl::StrCat("delta_from_largest_observed too high: ",
                     delta_from_largest_observed,
                     ", largest_acked: ", largest_acked.ToUint64())
            .c_str());
    return false;
  }

  // Time delta from the framer creation.
  uint32_t time_delta_us;
  if (!reader->ReadUInt32(&time_delta_us)) {
    set_detailed_error("Unable to read time delta in received packets.");
    return false;
  }

  QuicPacketNumber seq_num = largest_acked - delta_from_largest_observed;
  if (process_timestamps_) {
    last_timestamp_ = CalculateTimestampFromWire(time_delta_us);

    visitor_->OnAckTimestamp(seq_num, creation_time_ + last_timestamp_);
  }

  for (uint8_t i = 1; i < num_received_packets; ++i) {
    if (!reader->ReadUInt8(&delta_from_largest_observed)) {
      set_detailed_error("Unable to read sequence delta in received packets.");
      return false;
    }
    if (largest_acked.ToUint64() <= delta_from_largest_observed) {
      set_detailed_error(
          absl::StrCat("delta_from_largest_observed too high: ",
                       delta_from_largest_observed,
                       ", largest_acked: ", largest_acked.ToUint64())
              .c_str());
      return false;
    }
    seq_num = largest_acked - delta_from_largest_observed;

    // Time delta from the previous timestamp.
    uint64_t incremental_time_delta_us;
    if (!reader->ReadUFloat16(&incremental_time_delta_us)) {
      set_detailed_error(
          "Unable to read incremental time delta in received packets.");
      return false;
    }

    if (process_timestamps_) {
      last_timestamp_ = last_timestamp_ + QuicTime::Delta::FromMicroseconds(
                                              incremental_time_delta_us);
      visitor_->OnAckTimestamp(seq_num, creation_time_ + last_timestamp_);
    }
  }
  return true;
}

bool QuicFramer::ProcessIetfAckFrame(QuicDataReader* reader,
                                     uint64_t frame_type,
                                     QuicAckFrame* ack_frame) {
  uint64_t largest_acked;
  if (!reader->ReadVarInt62(&largest_acked)) {
    set_detailed_error("Unable to read largest acked.");
    return false;
  }
  if (largest_acked < first_sending_packet_number_.ToUint64()) {
    // Connection always sends packet starting from kFirstSendingPacketNumber >
    // 0, peer has observed an unsent packet.
    set_detailed_error("Largest acked is 0.");
    return false;
  }
  ack_frame->largest_acked = static_cast<QuicPacketNumber>(largest_acked);
  uint64_t ack_delay_time_in_us;
  if (!reader->ReadVarInt62(&ack_delay_time_in_us)) {
    set_detailed_error("Unable to read ack delay time.");
    return false;
  }

  if (ack_delay_time_in_us >=
      (quiche::kVarInt62MaxValue >> peer_ack_delay_exponent_)) {
    ack_frame->ack_delay_time = QuicTime::Delta::Infinite();
  } else {
    ack_delay_time_in_us = (ack_delay_time_in_us << peer_ack_delay_exponent_);
    ack_frame->ack_delay_time =
        QuicTime::Delta::FromMicroseconds(ack_delay_time_in_us);
  }
  if (!visitor_->OnAckFrameStart(QuicPacketNumber(largest_acked),
                                 ack_frame->ack_delay_time)) {
    // The visitor suppresses further processing of the packet. Although this is
    // not a parsing error, returns false as this is in middle of processing an
    // ACK frame.
    set_detailed_error("Visitor suppresses further processing of ACK frame.");
    return false;
  }

  // Get number of ACK blocks from the packet.
  uint64_t ack_block_count;
  if (!reader->ReadVarInt62(&ack_block_count)) {
    set_detailed_error("Unable to read ack block count.");
    return false;
  }
  // There always is a first ACK block, which is the (number of packets being
  // acked)-1, up to and including the packet at largest_acked. Therefore if the
  // value is 0, then only largest is acked. If it is 1, then largest-1,
  // largest] are acked, etc
  uint64_t ack_block_value;
  if (!reader->ReadVarInt62(&ack_block_value)) {
    set_detailed_error("Unable to read first ack block length.");
    return false;
  }
  // Calculate the packets being acked in the first block.
  //  +1 because AddRange implementation requires [low,high)
  uint64_t block_high = largest_acked + 1;
  uint64_t block_low = largest_acked - ack_block_value;

  // ack_block_value is the number of packets preceding the
  // largest_acked packet which are in the block being acked. Thus,
  // its maximum value is largest_acked-1. Test this, reporting an
  // error if the value is wrong.
  if (ack_block_value + first_sending_packet_number_.ToUint64() >
      largest_acked) {
    set_detailed_error(absl::StrCat("Underflow with first ack block length ",
                                    ack_block_value + 1, " largest acked is ",
                                    largest_acked, ".")
                           .c_str());
    return false;
  }

  if (!visitor_->OnAckRange(QuicPacketNumber(block_low),
                            QuicPacketNumber(block_high))) {
    // The visitor suppresses further processing of the packet. Although
    // this is not a parsing error, returns false as this is in middle
    // of processing an ACK frame.
    set_detailed_error("Visitor suppresses further processing of ACK frame.");
    return false;
  }

  while (ack_block_count != 0) {
    uint64_t gap_block_value;
    // Get the sizes of the gap and ack blocks,
    if (!reader->ReadVarInt62(&gap_block_value)) {
      set_detailed_error("Unable to read gap block value.");
      return false;
    }
    // It's an error if the gap is larger than the space from packet
    // number 0 to the start of the block that's just been acked, PLUS
    // there must be space for at least 1 packet to be acked. For
    // example, if block_low is 10 and gap_block_value is 9, it means
    // the gap block is 10 packets long, leaving no room for a packet
    // to be acked. Thus, gap_block_value+2 can not be larger than
    // block_low.
    // The test is written this way to detect wrap-arounds.
    if ((gap_block_value + 2) > block_low) {
      set_detailed_error(
          absl::StrCat("Underflow with gap block length ", gap_block_value + 1,
                       " previous ack block start is ", block_low, ".")
              .c_str());
      return false;
    }

    // Adjust block_high to be the top of the next ack block.
    // There is a gap of |gap_block_value| packets between the bottom
    // of ack block N and top of block N+1.  Note that gap_block_value
    // is he size of the gap minus 1 (per the QUIC protocol), and
    // block_high is the packet number of the first packet of the gap
    // (per the implementation of OnAckRange/AddAckRange, below).
    block_high = block_low - 1 - gap_block_value;

    if (!reader->ReadVarInt62(&ack_block_value)) {
      set_detailed_error("Unable to read ack block value.");
      return false;
    }
    if (ack_block_value + first_sending_packet_number_.ToUint64() >
        (block_high - 1)) {
      set_detailed_error(
          absl::StrCat("Underflow with ack block length ", ack_block_value + 1,
                       " latest ack block end is ", block_high - 1, ".")
              .c_str());
      return false;
    }
    // Calculate the low end of the new nth ack block. The +1 is
    // because the encoded value is the blocksize-1.
    block_low = block_high - 1 - ack_block_value;
    if (!visitor_->OnAckRange(QuicPacketNumber(block_low),
                              QuicPacketNumber(block_high))) {
      // The visitor suppresses further processing of the packet. Although
      // this is not a parsing error, returns false as this is in middle
      // of processing an ACK frame.
      set_detailed_error("Visitor suppresses further processing of ACK frame.");
      return false;
    }

    // Another one done.
    ack_block_count--;
  }

  QUICHE_DCHECK(!ack_frame->ecn_counters.has_value());
  if (frame_type == IETF_ACK_RECEIVE_TIMESTAMPS) {
    QUICHE_DCHECK(process_timestamps_);
    if (!ProcessIetfTimestampsInAckFrame(ack_frame->largest_acked, reader)) {
      return false;
    }
  } else if (frame_type == IETF_ACK_ECN) {
    ack_frame->ecn_counters = QuicEcnCounts();
    if (!reader->ReadVarInt62(&ack_frame->ecn_counters->ect0)) {
      set_detailed_error("Unable to read ack ect_0_count.");
      return false;
    }
    if (!reader->ReadVarInt62(&ack_frame->ecn_counters->ect1)) {
      set_detailed_error("Unable to read ack ect_1_count.");
      return false;
    }
    if (!reader->ReadVarInt62(&ack_frame->ecn_counters->ce)) {
      set_detailed_error("Unable to read ack ecn_ce_count.");
      return false;
    }
  }

  if (!visitor_->OnAckFrameEnd(QuicPacketNumber(block_low),
                               ack_frame->ecn_counters)) {
    set_detailed_error(
        "Error occurs when visitor finishes processing the ACK frame.");
    return false;
  }

  return true;
}

bool QuicFramer::ProcessIetfTimestampsInAckFrame(QuicPacketNumber largest_acked,
                                                 QuicDataReader* reader) {
  uint64_t timestamp_range_count;
  if (!reader->ReadVarInt62(&timestamp_range_count)) {
    set_detailed_error("Unable to read receive timestamp range count.");
    return false;
  }
  if (timestamp_range_count == 0) {
    return true;
  }

  QuicPacketNumber packet_number = largest_acked;

  // Iterate through all timestamp ranges, each of which represents a block of
  // contiguous packets for which receive timestamps are being reported. Each
  // range is of the form:
  //
  // Timestamp Range {
  //    Gap (i),
  //    Timestamp Delta Count (i),
  //    Timestamp Delta (i) ...,
  //  }
  for (uint64_t i = 0; i < timestamp_range_count; i++) {
    uint64_t gap;
    if (!reader->ReadVarInt62(&gap)) {
      set_detailed_error("Unable to read receive timestamp gap.");
      return false;
    }
    if (packet_number.ToUint64() < gap) {
      set_detailed_error("Receive timestamp gap too high.");
      return false;
    }
    packet_number = packet_number - gap;
    uint64_t timestamp_count;
    if (!reader->ReadVarInt62(&timestamp_count)) {
      set_detailed_error("Unable to read receive timestamp count.");
      return false;
    }
    if (packet_number.ToUint64() < timestamp_count) {
      set_detailed_error("Receive timestamp count too high.");
      return false;
    }
    for (uint64_t j = 0; j < timestamp_count; j++) {
      uint64_t timestamp_delta;
      if (!reader->ReadVarInt62(&timestamp_delta)) {
        set_detailed_error("Unable to read receive timestamp delta.");
        return false;
      }
      // The first timestamp delta is relative to framer creation time; whereas
      // subsequent deltas are relative to the previous delta in decreasing
      // packet order.
      timestamp_delta = timestamp_delta << receive_timestamps_exponent_;
      if (i == 0 && j == 0) {
        last_timestamp_ = QuicTime::Delta::FromMicroseconds(timestamp_delta);
      } else {
        last_timestamp_ = last_timestamp_ -
                          QuicTime::Delta::FromMicroseconds(timestamp_delta);
        if (last_timestamp_ < QuicTime::Delta::Zero()) {
          set_detailed_error("Receive timestamp delta too high.");
          return false;
        }
      }
      visitor_->OnAckTimestamp(packet_number, creation_time_ + last_timestamp_);
      packet_number--;
    }
    packet_number--;
  }
  return true;
}

bool QuicFramer::ProcessStopWaitingFrame(QuicDataReader* reader,
                                         const QuicPacketHeader& header,
                                         QuicStopWaitingFrame* stop_waiting) {
  uint64_t least_unacked_delta;
  if (!reader->ReadBytesToUInt64(header.packet_number_length,
                                 &least_unacked_delta)) {
    set_detailed_error("Unable to read least unacked delta.");
    return false;
  }
  if (header.packet_number.ToUint64() <= least_unacked_delta) {
    set_detailed_error("Invalid unacked delta.");
    return false;
  }
  stop_waiting->least_unacked = header.packet_number - least_unacked_delta;

  return true;
}

bool QuicFramer::ProcessRstStreamFrame(QuicDataReader* reader,
                                       QuicRstStreamFrame* frame) {
  if (!reader->ReadUInt32(&frame->stream_id)) {
    set_detailed_error("Unable to read stream_id.");
    return false;
  }

  if (!reader->ReadUInt64(&frame->byte_offset)) {
    set_detailed_error("Unable to read rst stream sent byte offset.");
    return false;
  }

  uint32_t error_code;
  if (!reader->ReadUInt32(&error_code)) {
    set_detailed_error("Unable to read rst stream error code.");
    return false;
  }

  if (error_code >= QUIC_STREAM_LAST_ERROR) {
    // Ignore invalid stream error code if any.
    error_code = QUIC_STREAM_LAST_ERROR;
  }

  frame->error_code = static_cast<QuicRstStreamErrorCode>(error_code);

  return true;
}

bool QuicFramer::ProcessConnectionCloseFrame(QuicDataReader* reader,
                                             QuicConnectionCloseFrame* frame) {
  uint32_t error_code;
  frame->close_type = GOOGLE_QUIC_CONNECTION_CLOSE;

  if (!reader->ReadUInt32(&error_code)) {
    set_detailed_error("Unable to read connection close error code.");
    return false;
  }

  // For Google QUIC connection closes, |wire_error_code| and |quic_error_code|
  // must have the same value.
  frame->wire_error_code = error_code;
  frame->quic_error_code = static_cast<QuicErrorCode>(error_code);

  absl::string_view error_details;
  if (!reader->ReadStringPiece16(&error_details)) {
    set_detailed_error("Unable to read connection close error details.");
    return false;
  }
  frame->error_details = std::string(error_details);

  return true;
}

bool QuicFramer::ProcessGoAwayFrame(QuicDataReader* reader,
                                    QuicGoAwayFrame* frame) {
  uint32_t error_code;
  if (!reader->ReadUInt32(&error_code)) {
    set_detailed_error("Unable to read go away error code.");
    return false;
  }

  frame->error_code = static_cast<QuicErrorCode>(error_code);

  uint32_t stream_id;
  if (!reader->ReadUInt32(&stream_id)) {
    set_detailed_error("Unable to read last good stream id.");
    return false;
  }
  frame->last_good_stream_id = static_cast<QuicStreamId>(stream_id);

  absl::string_view reason_phrase;
  if (!reader->ReadStringPiece16(&reason_phrase)) {
    set_detailed_error("Unable to read goaway reason.");
    return false;
  }
  frame->reason_phrase = std::string(reason_phrase);

  return true;
}

bool QuicFramer::ProcessWindowUpdateFrame(QuicDataReader* reader,
                                          QuicWindowUpdateFrame* frame) {
  if (!reader->ReadUInt32(&frame->stream_id)) {
    set_detailed_error("Unable to read stream_id.");
    return false;
  }

  if (!reader->ReadUInt64(&frame->max_data)) {
    set_detailed_error("Unable to read window byte_offset.");
    return false;
  }

  return true;
}

bool QuicFramer::ProcessBlockedFrame(QuicDataReader* reader,
                                     QuicBlockedFrame* frame) {
  QUICHE_DCHECK(!VersionHasIetfQuicFrames(version_.transport_version))
      << "Attempt to process non-IETF QUIC frames in an IETF QUIC version.";

  if (!reader->ReadUInt32(&frame->stream_id)) {
    set_detailed_error("Unable to read stream_id.");
    return false;
  }

  return true;
}

void QuicFramer::ProcessPaddingFrame(QuicDataReader* reader,
                                     QuicPaddingFrame* frame) {
  // Type byte has been read.
  frame->num_padding_bytes = 1;
  uint8_t next_byte;
  while (!reader->IsDoneReading() && reader->PeekByte() == 0x00) {
    reader->ReadBytes(&next_byte, 1);
    QUICHE_DCHECK_EQ(0x00, next_byte);
    ++frame->num_padding_bytes;
  }
}

bool QuicFramer::ProcessMessageFrame(QuicDataReader* reader,
                                     bool no_message_length,
                                     QuicMessageFrame* frame) {
  if (no_message_length) {
    absl::string_view remaining(reader->ReadRemainingPayload());
    frame->data = remaining.data();
    frame->message_length = remaining.length();
    return true;
  }

  uint64_t message_length;
  if (!reader->ReadVarInt62(&message_length)) {
    set_detailed_error("Unable to read message length");
    return false;
  }

  absl::string_view message_piece;
  if (!reader->ReadStringPiece(&message_piece, message_length)) {
    set_detailed_error("Unable to read message data");
    return false;
  }

  frame->data = message_piece.data();
  frame->message_length = message_length;

  return true;
}

// static
absl::string_view QuicFramer::GetAssociatedDataFromEncryptedPacket(
    QuicTransportVersion version, const QuicEncryptedPacket& encrypted,
    uint8_t destination_connection_id_length,
    uint8_t source_connection_id_length, bool includes_version,
    bool includes_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    quiche::QuicheVariableLengthIntegerLength retry_token_length_length,
    uint64_t retry_token_length,
    quiche::QuicheVariableLengthIntegerLength length_length) {
  // TODO(ianswett): This is identical to QuicData::AssociatedData.
  return absl::string_view(
      encrypted.data(),
      GetStartOfEncryptedData(version, destination_connection_id_length,
                              source_connection_id_length, includes_version,
                              includes_diversification_nonce,
                              packet_number_length, retry_token_length_length,
                              retry_token_length, length_length));
}

void QuicFramer::SetDecrypter(EncryptionLevel level,
                              std::unique_ptr<QuicDecrypter> decrypter) {
  QUICHE_DCHECK_GE(level, decrypter_level_);
  QUICHE_DCHECK(!version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Setting decrypter from level "
                << decrypter_level_ << " to " << level;
  decrypter_[decrypter_level_] = nullptr;
  decrypter_[level] = std::move(decrypter);
  decrypter_level_ = level;
}

void QuicFramer::SetAlternativeDecrypter(
    EncryptionLevel level, std::unique_ptr<QuicDecrypter> decrypter,
    bool latch_once_used) {
  QUICHE_DCHECK_NE(level, decrypter_level_);
  QUICHE_DCHECK(!version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Setting alternative decrypter from level "
                << alternative_decrypter_level_ << " to " << level;
  if (alternative_decrypter_level_ != NUM_ENCRYPTION_LEVELS) {
    decrypter_[alternative_decrypter_level_] = nullptr;
  }
  decrypter_[level] = std::move(decrypter);
  alternative_decrypter_level_ = level;
  alternative_decrypter_latch_ = latch_once_used;
}

void QuicFramer::InstallDecrypter(EncryptionLevel level,
                                  std::unique_ptr<QuicDecrypter> decrypter) {
  QUICHE_DCHECK(version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Installing decrypter at level " << level;
  decrypter_[level] = std::move(decrypter);
}

void QuicFramer::RemoveDecrypter(EncryptionLevel level) {
  QUICHE_DCHECK(version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Removing decrypter at level " << level;
  decrypter_[level] = nullptr;
}

void QuicFramer::SetKeyUpdateSupportForConnection(bool enabled) {
  QUIC_DVLOG(1) << ENDPOINT << "SetKeyUpdateSupportForConnection: " << enabled;
  support_key_update_for_connection_ = enabled;
}

void QuicFramer::DiscardPreviousOneRttKeys() {
  QUICHE_DCHECK(support_key_update_for_connection_);
  QUIC_DVLOG(1) << ENDPOINT << "Discarding previous set of 1-RTT keys";
  previous_decrypter_ = nullptr;
}

bool QuicFramer::DoKeyUpdate(KeyUpdateReason reason) {
  QUICHE_DCHECK(support_key_update_for_connection_);
  if (!next_decrypter_) {
    // If key update is locally initiated, next decrypter might not be created
    // yet.
    next_decrypter_ = visitor_->AdvanceKeysAndCreateCurrentOneRttDecrypter();
  }
  std::unique_ptr<QuicEncrypter> next_encrypter =
      visitor_->CreateCurrentOneRttEncrypter();
  if (!next_decrypter_ || !next_encrypter) {
    QUIC_BUG(quic_bug_10850_58) << "Failed to create next crypters";
    return false;
  }
  key_update_performed_ = true;
  current_key_phase_bit_ = !current_key_phase_bit_;
  QUIC_DLOG(INFO) << ENDPOINT << "DoKeyUpdate: new current_key_phase_bit_="
                  << current_key_phase_bit_;
  current_key_phase_first_received_packet_number_.Clear();
  previous_decrypter_ = std::move(decrypter_[ENCRYPTION_FORWARD_SECURE]);
  decrypter_[ENCRYPTION_FORWARD_SECURE] = std::move(next_decrypter_);
  encrypter_[ENCRYPTION_FORWARD_SECURE] = std::move(next_encrypter);
  switch (reason) {
    case KeyUpdateReason::kInvalid:
      QUIC_CODE_COUNT(quic_key_update_invalid);
      break;
    case KeyUpdateReason::kRemote:
      QUIC_CODE_COUNT(quic_key_update_remote);
      break;
    case KeyUpdateReason::kLocalForTests:
      QUIC_CODE_COUNT(quic_key_update_local_for_tests);
      break;
    case KeyUpdateReason::kLocalForInteropRunner:
      QUIC_CODE_COUNT(quic_key_update_local_for_interop_runner);
      break;
    case KeyUpdateReason::kLocalAeadConfidentialityLimit:
      QUIC_CODE_COUNT(quic_key_update_local_aead_confidentiality_limit);
      break;
    case KeyUpdateReason::kLocalKeyUpdateLimitOverride:
      QUIC_CODE_COUNT(quic_key_update_local_limit_override);
      break;
  }
  visitor_->OnKeyUpdate(reason);
  return true;
}

QuicPacketCount QuicFramer::PotentialPeerKeyUpdateAttemptCount() const {
  return potential_peer_key_update_attempt_count_;
}

const QuicDecrypter* QuicFramer::GetDecrypter(EncryptionLevel level) const {
  QUICHE_DCHECK(version_.KnowsWhichDecrypterToUse());
  return decrypter_[level].get();
}

const QuicDecrypter* QuicFramer::decrypter() const {
  return decrypter_[decrypter_level_].get();
}

const QuicDecrypter* QuicFramer::alternative_decrypter() const {
  if (alternative_decrypter_level_ == NUM_ENCRYPTION_LEVELS) {
    return nullptr;
  }
  return decrypter_[alternative_decrypter_level_].get();
}

void QuicFramer::SetEncrypter(EncryptionLevel level,
                              std::unique_ptr<QuicEncrypter> encrypter) {
  QUICHE_DCHECK_GE(level, 0);
  QUICHE_DCHECK_LT(level, NUM_ENCRYPTION_LEVELS);
  QUIC_DVLOG(1) << ENDPOINT << "Setting encrypter at level " << level;
  encrypter_[level] = std::move(encrypter);
}

void QuicFramer::RemoveEncrypter(EncryptionLevel level) {
  QUIC_DVLOG(1) << ENDPOINT << "Removing encrypter of " << level;
  encrypter_[level] = nullptr;
}

void QuicFramer::SetInitialObfuscators(QuicConnectionId connection_id) {
  CrypterPair crypters;
  CryptoUtils::CreateInitialObfuscators(perspective_, version_, connection_id,
                                        &crypters);
  encrypter_[ENCRYPTION_INITIAL] = std::move(crypters.encrypter);
  decrypter_[ENCRYPTION_INITIAL] = std::move(crypters.decrypter);
}

size_t QuicFramer::EncryptInPlace(EncryptionLevel level,
                                  QuicPacketNumber packet_number, size_t ad_len,
                                  size_t total_len, size_t buffer_len,
                                  char* buffer) {
  QUICHE_DCHECK(packet_number.IsInitialized());
  if (encrypter_[level] == nullptr) {
    QUIC_BUG(quic_bug_10850_59)
        << ENDPOINT
        << "Attempted to encrypt in place without encrypter at level " << level;
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }

  size_t output_length = 0;
  if (!encrypter_[level]->EncryptPacket(
          packet_number.ToUint64(),
          absl::string_view(buffer, ad_len),  // Associated data
          absl::string_view(buffer + ad_len,
                            total_len - ad_len),  // Plaintext
          buffer + ad_len,                        // Destination buffer
          &output_length, buffer_len - ad_len)) {
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }
  if (version_.HasHeaderProtection() &&
      !ApplyHeaderProtection(level, buffer, ad_len + output_length, ad_len)) {
    QUIC_DLOG(ERROR) << "Applying header protection failed.";
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }

  return ad_len + output_length;
}

namespace {

const size_t kHPSampleLen = 16;

constexpr bool IsLongHeader(uint8_t type_byte) {
  return (type_byte & FLAGS_LONG_HEADER) != 0;
}

}  // namespace

bool QuicFramer::ApplyHeaderProtection(EncryptionLevel level, char* buffer,
                                       size_t buffer_len, size_t ad_len) {
  QuicDataReader buffer_reader(buffer, buffer_len);
  QuicDataWriter buffer_writer(buffer_len, buffer);
  // The sample starts 4 bytes after the start of the packet number.
  if (ad_len < last_written_packet_number_length_) {
    return false;
  }
  size_t pn_offset = ad_len - last_written_packet_number_length_;
  // Sample the ciphertext and generate the mask to use for header protection.
  size_t sample_offset = pn_offset + 4;
  QuicDataReader sample_reader(buffer, buffer_len);
  absl::string_view sample;
  if (!sample_reader.Seek(sample_offset) ||
      !sample_reader.ReadStringPiece(&sample, kHPSampleLen)) {
    QUIC_BUG(quic_bug_10850_60)
        << "Not enough bytes to sample: sample_offset " << sample_offset
        << ", sample len: " << kHPSampleLen << ", buffer len: " << buffer_len;
    return false;
  }

  if (encrypter_[level] == nullptr) {
    QUIC_BUG(quic_bug_12975_8)
        << ENDPOINT
        << "Attempted to apply header protection without encrypter at level "
        << level << " using " << version_;
    return false;
  }

  std::string mask = encrypter_[level]->GenerateHeaderProtectionMask(sample);
  if (mask.empty()) {
    QUIC_BUG(quic_bug_10850_61) << "Unable to generate header protection mask.";
    return false;
  }
  QuicDataReader mask_reader(mask.data(), mask.size());

  // Apply the mask to the 4 or 5 least significant bits of the first byte.
  uint8_t bitmask = 0x1f;
  uint8_t type_byte;
  if (!buffer_reader.ReadUInt8(&type_byte)) {
    return false;
  }
  QuicLongHeaderType header_type;
  if (IsLongHeader(type_byte)) {
    bitmask = 0x0f;
    header_type = GetLongHeaderType(type_byte, version_);
    if (header_type == INVALID_PACKET_TYPE) {
      return false;
    }
  }
  uint8_t mask_byte;
  if (!mask_reader.ReadUInt8(&mask_byte) ||
      !buffer_writer.WriteUInt8(type_byte ^ (mask_byte & bitmask))) {
    return false;
  }

  // Adjust |pn_offset| to account for the diversification nonce.
  if (IsLongHeader(type_byte) && header_type == ZERO_RTT_PROTECTED &&
      perspective_ == Perspective::IS_SERVER &&
      version_.handshake_protocol == PROTOCOL_QUIC_CRYPTO) {
    if (pn_offset <= kDiversificationNonceSize) {
      QUIC_BUG(quic_bug_10850_62)
          << "Expected diversification nonce, but not enough bytes";
      return false;
    }
    pn_offset -= kDiversificationNonceSize;
  }
  // Advance the reader and writer to the packet number. Both the reader and
  // writer have each read/written one byte.
  if (!buffer_writer.Seek(pn_offset - 1) ||
      !buffer_reader.Seek(pn_offset - 1)) {
    return false;
  }
  // Apply the rest of the mask to the packet number.
  for (size_t i = 0; i < last_written_packet_number_length_; ++i) {
    uint8_t buffer_byte;
    uint8_t pn_mask_byte;
    if (!mask_reader.ReadUInt8(&pn_mask_byte) ||
        !buffer_reader.ReadUInt8(&buffer_byte) ||
        !buffer_writer.WriteUInt8(buffer_byte ^ pn_mask_byte)) {
      return false;
    }
  }
  return true;
}

bool QuicFramer::RemoveHeaderProtection(
    QuicDataReader* reader, const QuicEncryptedPacket& packet,
    QuicDecrypter& decrypter, Perspective perspective,
    const ParsedQuicVersion& version, QuicPacketNumber base_packet_number,
    QuicPacketHeader* header, uint64_t* full_packet_number,
    AssociatedDataStorage& associated_data) {
  bool has_diversification_nonce =
      header->form == IETF_QUIC_LONG_HEADER_PACKET &&
      header->long_packet_type == ZERO_RTT_PROTECTED &&
      perspective == Perspective::IS_CLIENT &&
      version.handshake_protocol == PROTOCOL_QUIC_CRYPTO;

  // Read a sample from the ciphertext and compute the mask to use for header
  // protection.
  absl::string_view remaining_packet = reader->PeekRemainingPayload();
  QuicDataReader sample_reader(remaining_packet);

  // The sample starts 4 bytes after the start of the packet number.
  absl::string_view pn;
  if (!sample_reader.ReadStringPiece(&pn, 4)) {
    QUIC_DVLOG(1) << "Not enough data to sample";
    return false;
  }
  if (has_diversification_nonce) {
    // In Google QUIC, the diversification nonce comes between the packet number
    // and the sample.
    if (!sample_reader.Seek(kDiversificationNonceSize)) {
      QUIC_DVLOG(1) << "No diversification nonce to skip over";
      return false;
    }
  }
  std::string mask = decrypter.GenerateHeaderProtectionMask(&sample_reader);
  QuicDataReader mask_reader(mask.data(), mask.size());
  if (mask.empty()) {
    QUIC_DVLOG(1) << "Failed to compute mask";
    return false;
  }

  // Unmask the rest of the type byte.
  uint8_t bitmask = 0x1f;
  if (IsLongHeader(header->type_byte)) {
    bitmask = 0x0f;
  }
  uint8_t mask_byte;
  if (!mask_reader.ReadUInt8(&mask_byte)) {
    QUIC_DVLOG(1) << "No first byte to read from mask";
    return false;
  }
  header->type_byte ^= (mask_byte & bitmask);

  // Compute the packet number length.
  header->packet_number_length =
      static_cast<QuicPacketNumberLength>((header->type_byte & 0x03) + 1);

  char pn_buffer[IETF_MAX_PACKET_NUMBER_LENGTH] = {};
  QuicDataWriter pn_writer(ABSL_ARRAYSIZE(pn_buffer), pn_buffer);

  // Read the (protected) packet number from the reader and unmask the packet
  // number.
  for (size_t i = 0; i < header->packet_number_length; ++i) {
    uint8_t protected_pn_byte, pn_mask_byte;
    if (!mask_reader.ReadUInt8(&pn_mask_byte) ||
        !reader->ReadUInt8(&protected_pn_byte) ||
        !pn_writer.WriteUInt8(protected_pn_byte ^ pn_mask_byte)) {
      QUIC_DVLOG(1) << "Failed to unmask packet number";
      return false;
    }
  }
  QuicDataReader packet_number_reader(pn_writer.data(), pn_writer.length());
  if (!ProcessAndCalculatePacketNumber(
          &packet_number_reader, header->packet_number_length,
          base_packet_number, full_packet_number)) {
    return false;
  }

  // Get the associated data, and apply the same unmasking operations to it.
  absl::string_view ad = GetAssociatedDataFromEncryptedPacket(
      version.transport_version, packet,
      GetIncludedDestinationConnectionIdLength(*header),
      GetIncludedSourceConnectionIdLength(*header), header->version_flag,
      has_diversification_nonce, header->packet_number_length,
      header->retry_token_length_length, header->retry_token.length(),
      header->length_length);
  associated_data.assign(ad.begin(), ad.end());
  QuicDataWriter ad_writer(associated_data.size(), associated_data.data());

  // Apply the unmasked type byte and packet number to |associated_data|.
  if (!ad_writer.WriteUInt8(header->type_byte)) {
    return false;
  }
  // Put the packet number at the end of the AD, or if there's a diversification
  // nonce, before that (which is at the end of the AD).
  size_t seek_len = ad_writer.remaining() - header->packet_number_length;
  if (has_diversification_nonce) {
    seek_len -= kDiversificationNonceSize;
  }
  if (!ad_writer.Seek(seek_len) ||
      !ad_writer.WriteBytes(pn_writer.data(), pn_writer.length())) {
    QUIC_DVLOG(1) << "Failed to apply unmasking operations to AD";
    return false;
  }

  return true;
}

size_t QuicFramer::EncryptPayload(EncryptionLevel level,
                                  QuicPacketNumber packet_number,
                                  const QuicPacket& packet, char* buffer,
                                  size_t buffer_len) {
  QUICHE_DCHECK(packet_number.IsInitialized());
  if (encrypter_[level] == nullptr) {
    QUIC_BUG(quic_bug_10850_63)
        << ENDPOINT << "Attempted to encrypt without encrypter at level "
        << level;
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }

  absl::string_view associated_data =
      packet.AssociatedData(version_.transport_version);
  // Copy in the header, because the encrypter only populates the encrypted
  // plaintext content.
  const size_t ad_len = associated_data.length();
  if (packet.length() < ad_len) {
    QUIC_BUG(quic_bug_10850_64)
        << ENDPOINT << "packet is shorter than associated data length. version:"
        << version() << ", packet length:" << packet.length()
        << ", associated data length:" << ad_len;
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }
  memmove(buffer, associated_data.data(), ad_len);
  // Encrypt the plaintext into the buffer.
  size_t output_length = 0;
  if (!encrypter_[level]->EncryptPacket(
          packet_number.ToUint64(), associated_data,
          packet.Plaintext(version_.transport_version), buffer + ad_len,
          &output_length, buffer_len - ad_len)) {
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }
  if (version_.HasHeaderProtection() &&
      !ApplyHeaderProtection(level, buffer, ad_len + output_length, ad_len)) {
    QUIC_DLOG(ERROR) << "Applying header protection failed.";
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }

  return ad_len + output_length;
}

size_t QuicFramer::GetCiphertextSize(EncryptionLevel level,
                                     size_t plaintext_size) const {
  if (encrypter_[level] == nullptr) {
    QUIC_BUG(quic_bug_10850_65)
        << ENDPOINT
        << "Attempted to get ciphertext size without encrypter at level "
        << level << " using " << version_;
    return plaintext_size;
  }
  return encrypter_[level]->GetCiphertextSize(plaintext_size);
}

size_t QuicFramer::GetMaxPlaintextSize(size_t ciphertext_size) {
  // In order to keep the code simple, we don't have the current encryption
  // level to hand. Both the NullEncrypter and AES-GCM have a tag length of 12.
  size_t min_plaintext_size = ciphertext_size;

  for (int i = ENCRYPTION_INITIAL; i < NUM_ENCRYPTION_LEVELS; i++) {
    if (encrypter_[i] != nullptr) {
      size_t size = encrypter_[i]->GetMaxPlaintextSize(ciphertext_size);
      if (size < min_plaintext_size) {
        min_plaintext_size = size;
      }
    }
  }

  return min_plaintext_size;
}

QuicPacketCount QuicFramer::GetOneRttEncrypterConfidentialityLimit() const {
  if (!encrypter_[ENCRYPTION_FORWARD_SECURE]) {
    QUIC_BUG(quic_bug_10850_66) << "1-RTT encrypter not set";
    return 0;
  }
  return encrypter_[ENCRYPTION_FORWARD_SECURE]->GetConfidentialityLimit();
}

bool QuicFramer::DecryptPayload(size_t udp_packet_length,
                                absl::string_view encrypted,
                                absl::string_view associated_data,
                                const QuicPacketHeader& header,
                                char* decrypted_buffer, size_t buffer_length,
                                size_t* decrypted_length,
                                EncryptionLevel* decrypted_level) {
  if (!EncryptionLevelIsValid(decrypter_level_)) {
    QUIC_BUG(quic_bug_10850_67)
        << "Attempted to decrypt with bad decrypter_level_";
    return false;
  }
  EncryptionLevel level = decrypter_level_;
  QuicDecrypter* decrypter = decrypter_[level].get();
  QuicDecrypter* alternative_decrypter = nullptr;
  bool key_phase_parsed = false;
  bool key_phase;
  bool attempt_key_update = false;
  if (version().KnowsWhichDecrypterToUse()) {
    if (header.form == GOOGLE_QUIC_PACKET) {
      QUIC_BUG(quic_bug_10850_68)
          << "Attempted to decrypt GOOGLE_QUIC_PACKET with a version that "
             "knows which decrypter to use";
      return false;
    }
    level = GetEncryptionLevel(header);
    if (!EncryptionLevelIsValid(level)) {
      QUIC_BUG(quic_bug_10850_69) << "Attempted to decrypt with bad level";
      return false;
    }
    decrypter = decrypter_[level].get();
    if (decrypter == nullptr) {
      return false;
    }
    if (level == ENCRYPTION_ZERO_RTT &&
        perspective_ == Perspective::IS_CLIENT && header.nonce != nullptr) {
      decrypter->SetDiversificationNonce(*header.nonce);
    }
    if (support_key_update_for_connection_ &&
        header.form == IETF_QUIC_SHORT_HEADER_PACKET) {
      QUICHE_DCHECK(version().UsesTls());
      QUICHE_DCHECK_EQ(level, ENCRYPTION_FORWARD_SECURE);
      key_phase = (header.type_byte & FLAGS_KEY_PHASE_BIT) != 0;
      key_phase_parsed = true;
      QUIC_DVLOG(1) << ENDPOINT << "packet " << header.packet_number
                    << " received key_phase=" << key_phase
                    << " current_key_phase_bit_=" << current_key_phase_bit_;
      if (key_phase != current_key_phase_bit_) {
        if ((current_key_phase_first_received_packet_number_.IsInitialized() &&
             header.packet_number >
                 current_key_phase_first_received_packet_number_) ||
            (!current_key_phase_first_received_packet_number_.IsInitialized() &&
             !key_update_performed_)) {
          if (!next_decrypter_) {
            next_decrypter_ =
                visitor_->AdvanceKeysAndCreateCurrentOneRttDecrypter();
            if (!next_decrypter_) {
              QUIC_BUG(quic_bug_10850_70) << "Failed to create next_decrypter";
              return false;
            }
          }
          QUIC_DVLOG(1) << ENDPOINT << "packet " << header.packet_number
                        << " attempt_key_update=true";
          attempt_key_update = true;
          potential_peer_key_update_attempt_count_++;
          decrypter = next_decrypter_.get();
        } else {
          if (previous_decrypter_) {
            QUIC_DVLOG(1) << ENDPOINT
                          << "trying previous_decrypter_ for packet "
                          << header.packet_number;
            decrypter = previous_decrypter_.get();
          } else {
            QUIC_DVLOG(1) << ENDPOINT << "dropping packet "
                          << header.packet_number << " with old key phase";
            return false;
          }
        }
      }
    }
  } else if (alternative_decrypter_level_ != NUM_ENCRYPTION_LEVELS) {
    if (!EncryptionLevelIsValid(alternative_decrypter_level_)) {
      QUIC_BUG(quic_bug_10850_71)
          << "Attempted to decrypt with bad alternative_decrypter_level_";
      return false;
    }
    alternative_decrypter = decrypter_[alternative_decrypter_level_].get();
  }

  if (decrypter == nullptr) {
    QUIC_BUG(quic_bug_10850_72)
        << "Attempting to decrypt without decrypter, encryption level:" << level
        << " version:" << version();
    return false;
  }

  bool success = decrypter->DecryptPacket(
      header.packet_number.ToUint64(), associated_data, encrypted,
      decrypted_buffer, decrypted_length, buffer_length);
  if (success) {
    visitor_->OnDecryptedPacket(udp_packet_length, level);
    if (level == ENCRYPTION_ZERO_RTT &&
        current_key_phase_first_received_packet_number_.IsInitialized() &&
        header.packet_number >
            current_key_phase_first_received_packet_number_) {
      set_detailed_error(absl::StrCat(
          "Decrypted a 0-RTT packet with a packet number ",
          header.packet_number.ToString(),
          " which is higher than a 1-RTT packet number ",
          current_key_phase_first_received_packet_number_.ToString()));
      return RaiseError(QUIC_INVALID_0RTT_PACKET_NUMBER_OUT_OF_ORDER);
    }
    *decrypted_level = level;
    potential_peer_key_update_attempt_count_ = 0;
    if (attempt_key_update) {
      if (!DoKeyUpdate(KeyUpdateReason::kRemote)) {
        set_detailed_error("Key update failed due to internal error");
        return RaiseError(QUIC_INTERNAL_ERROR);
      }
      QUICHE_DCHECK_EQ(current_key_phase_bit_, key_phase);
    }
    if (key_phase_parsed &&
        !current_key_phase_first_received_packet_number_.IsInitialized() &&
        key_phase == current_key_phase_bit_) {
      // Set packet number for current key phase if it hasn't been initialized
      // yet. This is set outside of attempt_key_update since the key update
      // may have been initiated locally, and in that case we don't know yet
      // which packet number from the remote side to use until we receive a
      // packet with that phase.
      QUIC_DVLOG(1) << ENDPOINT
                    << "current_key_phase_first_received_packet_number_ = "
                    << header.packet_number;
      current_key_phase_first_received_packet_number_ = header.packet_number;
      visitor_->OnDecryptedFirstPacketInKeyPhase();
    }
  } else if (alternative_decrypter != nullptr) {
    if (header.nonce != nullptr) {
      QUICHE_DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
      alternative_decrypter->SetDiversificationNonce(*header.nonce);
    }
    bool try_alternative_decryption = true;
    if (alternative_decrypter_level_ == ENCRYPTION_ZERO_RTT) {
      if (perspective_ == Perspective::IS_CLIENT) {
        if (header.nonce == nullptr) {
          // Can not use INITIAL decryption without a diversification nonce.
          try_alternative_decryption = false;
        }
      } else {
        QUICHE_DCHECK(header.nonce == nullptr);
      }
    }

    if (try_alternative_decryption) {
      success = alternative_decrypter->DecryptPacket(
          header.packet_number.ToUint64(), associated_data, encrypted,
          decrypted_buffer, decrypted_length, buffer_length);
    }
    if (success) {
      visitor_->OnDecryptedPacket(udp_packet_length,
                                  alternative_decrypter_level_);
      *decrypted_level = decrypter_level_;
      if (alternative_decrypter_latch_) {
        if (!EncryptionLevelIsValid(alternative_decrypter_level_)) {
          QUIC_BUG(quic_bug_10850_73)
              << "Attempted to latch alternate decrypter with bad "
                 "alternative_decrypter_level_";
          return false;
        }
        // Switch to the alternative decrypter and latch so that we cannot
        // switch back.
        decrypter_level_ = alternative_decrypter_level_;
        alternative_decrypter_level_ = NUM_ENCRYPTION_LEVELS;
      } else {
        // Switch the alternative decrypter so that we use it first next time.
        EncryptionLevel alt_level = alternative_decrypter_level_;
        alternative_decrypter_level_ = decrypter_level_;
        decrypter_level_ = alt_level;
      }
    }
  }

  if (!success) {
    QUIC_DVLOG(1) << ENDPOINT << "DecryptPacket failed for: " << header;
    return false;
  }

  return true;
}

size_t QuicFramer::GetIetfAckFrameSize(const QuicAckFrame& frame) {
  // Type byte, largest_acked, and delay_time are straight-forward.
  size_t ack_frame_size = kQuicFrameTypeSize;
  QuicPacketNumber largest_acked = LargestAcked(frame);
  ack_frame_size += QuicDataWriter::GetVarInt62Len(largest_acked.ToUint64());
  uint64_t ack_delay_time_us;
  ack_delay_time_us = frame.ack_delay_time.ToMicroseconds();
  ack_delay_time_us = ack_delay_time_us >> local_ack_delay_exponent_;
  ack_frame_size += QuicDataWriter::GetVarInt62Len(ack_delay_time_us);

  if (frame.packets.Empty() || frame.packets.Max() != largest_acked) {
    QUIC_BUG(quic_bug_10850_74) << "Malformed ack frame";
    // ACK frame serialization will fail and connection will be closed.
    return ack_frame_size;
  }

  // Ack block count.
  ack_frame_size +=
      QuicDataWriter::GetVarInt62Len(frame.packets.NumIntervals() - 1);

  // First Ack range.
  auto iter = frame.packets.rbegin();
  ack_frame_size += QuicDataWriter::GetVarInt62Len(iter->Length() - 1);
  QuicPacketNumber previous_smallest = iter->min();
  ++iter;

  // Ack blocks.
  for (; iter != frame.packets.rend(); ++iter) {
    const uint64_t gap = previous_smallest - iter->max() - 1;
    const uint64_t ack_range = iter->Length() - 1;
    ack_frame_size += (QuicDataWriter::GetVarInt62Len(gap) +
                       QuicDataWriter::GetVarInt62Len(ack_range));
    previous_smallest = iter->min();
  }

  if (UseIetfAckWithReceiveTimestamp(frame)) {
    ack_frame_size += GetIetfAckFrameTimestampSize(frame);
  } else {
    ack_frame_size += AckEcnCountSize(frame);
  }

  return ack_frame_size;
}

size_t QuicFramer::GetIetfAckFrameTimestampSize(const QuicAckFrame& ack) {
  QUICHE_DCHECK(!ack.received_packet_times.empty());
  std::string detailed_error;
  absl::InlinedVector<AckTimestampRange, 2> timestamp_ranges =
      GetAckTimestampRanges(ack, detailed_error);
  if (!detailed_error.empty()) {
    return 0;
  }

  int64_t size =
      FrameAckTimestampRanges(ack, timestamp_ranges, /*writer=*/nullptr);
  return std::max<int64_t>(0, size);
}

size_t QuicFramer::GetAckFrameSize(
    const QuicAckFrame& ack, QuicPacketNumberLength /*packet_number_length*/) {
  QUICHE_DCHECK(!ack.packets.Empty());
  size_t ack_size = 0;

  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return GetIetfAckFrameSize(ack);
  }
  AckFrameInfo ack_info = GetAckFrameInfo(ack);
  QuicPacketNumberLength ack_block_length =
      GetMinPacketNumberLength(QuicPacketNumber(ack_info.max_block_length));

  ack_size = GetMinAckFrameSize(version_.transport_version, ack,
                                local_ack_delay_exponent_,
                                UseIetfAckWithReceiveTimestamp(ack));
  // First ack block length.
  ack_size += ack_block_length;
  if (ack_info.num_ack_blocks != 0) {
    ack_size += kNumberOfAckBlocksSize;
    ack_size += std::min(ack_info.num_ack_blocks, kMaxAckBlocks) *
                (ack_block_length + PACKET_1BYTE_PACKET_NUMBER);
  }

  // Include timestamps.
  if (process_timestamps_) {
    ack_size += GetAckFrameTimeStampSize(ack);
  }

  return ack_size;
}

size_t QuicFramer::GetAckFrameTimeStampSize(const QuicAckFrame& ack) {
  if (ack.received_packet_times.empty()) {
    return 0;
  }

  return kQuicNumTimestampsLength + kQuicFirstTimestampLength +
         (kQuicTimestampLength + kQuicTimestampPacketNumberGapLength) *
             (ack.received_packet_times.size() - 1);
}

size_t QuicFramer::ComputeFrameLength(
    const QuicFrame& frame, bool last_frame_in_packet,
    QuicPacketNumberLength packet_number_length) {
  switch (frame.type) {
    case STREAM_FRAME:
      return GetMinStreamFrameSize(
                 version_.transport_version, frame.stream_frame.stream_id,
                 frame.stream_frame.offset, last_frame_in_packet,
                 frame.stream_frame.data_length) +
             frame.stream_frame.data_length;
    case CRYPTO_FRAME:
      return GetMinCryptoFrameSize(frame.crypto_frame->offset,
                                   frame.crypto_frame->data_length) +
             frame.crypto_frame->data_length;
    case ACK_FRAME: {
      return GetAckFrameSize(*frame.ack_frame, packet_number_length);
    }
    case STOP_WAITING_FRAME:
      return GetStopWaitingFrameSize(packet_number_length);
    case MTU_DISCOVERY_FRAME:
      // MTU discovery frames are serialized as ping frames.
      return kQuicFrameTypeSize;
    case MESSAGE_FRAME:
      return GetMessageFrameSize(last_frame_in_packet,
                                 frame.message_frame->message_length);
    case PADDING_FRAME:
      QUICHE_DCHECK(false);
      return 0;
    default:
      return GetRetransmittableControlFrameSize(version_.transport_version,
                                                frame);
  }
}

bool QuicFramer::AppendTypeByte(const QuicFrame& frame,
                                bool last_frame_in_packet,
                                QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return AppendIetfFrameType(frame, last_frame_in_packet, writer);
  }
  uint8_t type_byte = 0;
  switch (frame.type) {
    case STREAM_FRAME:
      type_byte =
          GetStreamFrameTypeByte(frame.stream_frame, last_frame_in_packet);
      break;
    case ACK_FRAME:
      return true;
    case MTU_DISCOVERY_FRAME:
      type_byte = static_cast<uint8_t>(PING_FRAME);
      break;
    case NEW_CONNECTION_ID_FRAME:
      set_detailed_error(
          "Attempt to append NEW_CONNECTION_ID frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case RETIRE_CONNECTION_ID_FRAME:
      set_detailed_error(
          "Attempt to append RETIRE_CONNECTION_ID frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case NEW_TOKEN_FRAME:
      set_detailed_error(
          "Attempt to append NEW_TOKEN frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case MAX_STREAMS_FRAME:
      set_detailed_error(
          "Attempt to append MAX_STREAMS frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case STREAMS_BLOCKED_FRAME:
      set_detailed_error(
          "Attempt to append STREAMS_BLOCKED frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case PATH_RESPONSE_FRAME:
      set_detailed_error(
          "Attempt to append PATH_RESPONSE frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case PATH_CHALLENGE_FRAME:
      set_detailed_error(
          "Attempt to append PATH_CHALLENGE frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case STOP_SENDING_FRAME:
      set_detailed_error(
          "Attempt to append STOP_SENDING frame and not in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case MESSAGE_FRAME:
      return true;

    default:
      type_byte = static_cast<uint8_t>(frame.type);
      break;
  }

  return writer->WriteUInt8(type_byte);
}

bool QuicFramer::AppendIetfFrameType(const QuicFrame& frame,
                                     bool last_frame_in_packet,
                                     QuicDataWriter* writer) {
  uint8_t type_byte = 0;
  switch (frame.type) {
    case PADDING_FRAME:
      type_byte = IETF_PADDING;
      break;
    case RST_STREAM_FRAME:
      type_byte = IETF_RST_STREAM;
      break;
    case CONNECTION_CLOSE_FRAME:
      switch (frame.connection_close_frame->close_type) {
        case IETF_QUIC_APPLICATION_CONNECTION_CLOSE:
          type_byte = IETF_APPLICATION_CLOSE;
          break;
        case IETF_QUIC_TRANSPORT_CONNECTION_CLOSE:
          type_byte = IETF_CONNECTION_CLOSE;
          break;
        default:
          set_detailed_error(absl::StrCat(
              "Invalid QuicConnectionCloseFrame type: ",
              static_cast<int>(frame.connection_close_frame->close_type)));
          return RaiseError(QUIC_INTERNAL_ERROR);
      }
      break;
    case GOAWAY_FRAME:
      set_detailed_error(
          "Attempt to create non-IETF QUIC GOAWAY frame in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case WINDOW_UPDATE_FRAME:
      // Depending on whether there is a stream ID or not, will be either a
      // MAX_STREAM_DATA frame or a MAX_DATA frame.
      if (frame.window_update_frame.stream_id ==
          QuicUtils::GetInvalidStreamId(transport_version())) {
        type_byte = IETF_MAX_DATA;
      } else {
        type_byte = IETF_MAX_STREAM_DATA;
      }
      break;
    case BLOCKED_FRAME:
      if (frame.blocked_frame.stream_id ==
          QuicUtils::GetInvalidStreamId(transport_version())) {
        type_byte = IETF_DATA_BLOCKED;
      } else {
        type_byte = IETF_STREAM_DATA_BLOCKED;
      }
      break;
    case STOP_WAITING_FRAME:
      set_detailed_error(
          "Attempt to append type byte of STOP WAITING frame in IETF QUIC.");
      return RaiseError(QUIC_INTERNAL_ERROR);
    case PING_FRAME:
      type_byte = IETF_PING;
      break;
    case STREAM_FRAME:
      type_byte =
          GetStreamFrameTypeByte(frame.stream_frame, last_frame_in_packet);
      break;
    case ACK_FRAME:
      // Do nothing here, AppendIetfAckFrameAndTypeByte() will put the type byte
      // in the buffer.
      return true;
    case MTU_DISCOVERY_FRAME:
      // The path MTU discovery frame is encoded as a PING frame on the wire.
      type_byte = IETF_PING;
      break;
    case NEW_CONNECTION_ID_FRAME:
      type_byte = IETF_NEW_CONNECTION_ID;
      break;
    case RETIRE_CONNECTION_ID_FRAME:
      type_byte = IETF_RETIRE_CONNECTION_ID;
      break;
    case NEW_TOKEN_FRAME:
      type_byte = IETF_NEW_TOKEN;
      break;
    case MAX_STREAMS_FRAME:
      if (frame.max_streams_frame.unidirectional) {
        type_byte = IETF_MAX_STREAMS_UNIDIRECTIONAL;
      } else {
        type_byte = IETF_MAX_STREAMS_BIDIRECTIONAL;
      }
      break;
    case STREAMS_BLOCKED_FRAME:
      if (frame.streams_blocked_frame.unidirectional) {
        type_byte = IETF_STREAMS_BLOCKED_UNIDIRECTIONAL;
      } else {
        type_byte = IETF_STREAMS_BLOCKED_BIDIRECTIONAL;
      }
      break;
    case PATH_RESPONSE_FRAME:
      type_byte = IETF_PATH_RESPONSE;
      break;
    case PATH_CHALLENGE_FRAME:
      type_byte = IETF_PATH_CHALLENGE;
      break;
    case STOP_SENDING_FRAME:
      type_byte = IETF_STOP_SENDING;
      break;
    case MESSAGE_FRAME:
      return true;
    case CRYPTO_FRAME:
      type_byte = IETF_CRYPTO;
      break;
    case HANDSHAKE_DONE_FRAME:
      type_byte = IETF_HANDSHAKE_DONE;
      break;
    case ACK_FREQUENCY_FRAME:
      type_byte = IETF_ACK_FREQUENCY;
      break;
    case IMMEDIATE_ACK_FRAME:
      type_byte = IETF_IMMEDIATE_ACK;
      break;
    case RESET_STREAM_AT_FRAME:
      type_byte = IETF_RESET_STREAM_AT;
      break;
    default:
      QUIC_BUG(quic_bug_10850_75)
          << "Attempt to generate a frame type for an unsupported value: "
          << frame.type;
      return false;
  }
  return writer->WriteVarInt62(type_byte);
}

// static
bool QuicFramer::AppendPacketNumber(QuicPacketNumberLength packet_number_length,
                                    QuicPacketNumber packet_number,
                                    QuicDataWriter* writer) {
  QUICHE_DCHECK(packet_number.IsInitialized());
  if (!IsValidPacketNumberLength(packet_number_length)) {
    QUIC_BUG(quic_bug_10850_76)
        << "Invalid packet_number_length: " << packet_number_length;
    return false;
  }
  return writer->WriteBytesToUInt64(packet_number_length,
                                    packet_number.ToUint64());
}

// static
bool QuicFramer::AppendStreamId(size_t stream_id_length, QuicStreamId stream_id,
                                QuicDataWriter* writer) {
  if (stream_id_length == 0 || stream_id_length > 4) {
    QUIC_BUG(quic_bug_10850_77)
        << "Invalid stream_id_length: " << stream_id_length;
    return false;
  }
  return writer->WriteBytesToUInt64(stream_id_length, stream_id);
}

// static
bool QuicFramer::AppendStreamOffset(size_t offset_length,
                                    QuicStreamOffset offset,
                                    QuicDataWriter* writer) {
  if (offset_length == 1 || offset_length > 8) {
    QUIC_BUG(quic_bug_10850_78)
        << "Invalid stream_offset_length: " << offset_length;
    return false;
  }

  return writer->WriteBytesToUInt64(offset_length, offset);
}

// static
bool QuicFramer::AppendAckBlock(uint8_t gap,
                                QuicPacketNumberLength length_length,
                                uint64_t length, QuicDataWriter* writer) {
  if (length == 0) {
    if (!IsValidPacketNumberLength(length_length)) {
      QUIC_BUG(quic_bug_10850_79)
          << "Invalid packet_number_length: " << length_length;
      return false;
    }
    return writer->WriteUInt8(gap) &&
           writer->WriteBytesToUInt64(length_length, length);
  }
  return writer->WriteUInt8(gap) &&
         AppendPacketNumber(length_length, QuicPacketNumber(length), writer);
}

bool QuicFramer::AppendStreamFrame(const QuicStreamFrame& frame,
                                   bool no_stream_frame_length,
                                   QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return AppendIetfStreamFrame(frame, no_stream_frame_length, writer);
  }
  if (!AppendStreamId(GetStreamIdSize(frame.stream_id), frame.stream_id,
                      writer)) {
    QUIC_BUG(quic_bug_10850_80) << "Writing stream id size failed.";
    return false;
  }
  if (!AppendStreamOffset(GetStreamOffsetSize(frame.offset), frame.offset,
                          writer)) {
    QUIC_BUG(quic_bug_10850_81) << "Writing offset size failed.";
    return false;
  }
  if (!no_stream_frame_length) {
    static_assert(
        std::numeric_limits<decltype(frame.data_length)>::max() <=
            std::numeric_limits<uint16_t>::max(),
        "If frame.data_length can hold more than a uint16_t than we need to "
        "check that frame.data_length <= std::numeric_limits<uint16_t>::max()");
    if (!writer->WriteUInt16(static_cast<uint16_t>(frame.data_length))) {
      QUIC_BUG(quic_bug_10850_82) << "Writing stream frame length failed";
      return false;
    }
  }

  if (data_producer_ != nullptr) {
    QUICHE_DCHECK_EQ(nullptr, frame.data_buffer);
    if (frame.data_length == 0) {
      return true;
    }
    if (data_producer_->WriteStreamData(frame.stream_id, frame.offset,
                                        frame.data_length,
                                        writer) != WRITE_SUCCESS) {
      QUIC_BUG(quic_bug_10850_83) << "Writing frame data failed.";
      return false;
    }
    return true;
  }

  if (!writer->WriteBytes(frame.data_buffer, frame.data_length)) {
    QUIC_BUG(quic_bug_10850_84) << "Writing frame data failed.";
    return false;
  }
  return true;
}

bool QuicFramer::AppendNewTokenFrame(const QuicNewTokenFrame& frame,
                                     QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.token.length()))) {
    set_detailed_error("Writing token length failed.");
    return false;
  }
  if (!writer->WriteBytes(frame.token.data(), frame.token.length())) {
    set_detailed_error("Writing token buffer failed.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessNewTokenFrame(QuicDataReader* reader,
                                      QuicNewTokenFrame* frame) {
  uint64_t length;
  if (!reader->ReadVarInt62(&length)) {
    set_detailed_error("Unable to read new token length.");
    return false;
  }
  if (length > kMaxNewTokenTokenLength) {
    set_detailed_error("Token length larger than maximum.");
    return false;
  }

  // TODO(ianswett): Don't use absl::string_view as an intermediary.
  absl::string_view data;
  if (!reader->ReadStringPiece(&data, length)) {
    set_detailed_error("Unable to read new token data.");
    return false;
  }
  frame->token = std::string(data);
  return true;
}

// Add a new ietf-format stream frame.
// Bits controlling whether there is a frame-length and frame-offset
// are in the QuicStreamFrame.
bool QuicFramer::AppendIetfStreamFrame(const QuicStreamFrame& frame,
                                       bool last_frame_in_packet,
                                       QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.stream_id))) {
    set_detailed_error("Writing stream id failed.");
    return false;
  }

  if (frame.offset != 0) {
    if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.offset))) {
      set_detailed_error("Writing data offset failed.");
      return false;
    }
  }

  if (!last_frame_in_packet) {
    if (!writer->WriteVarInt62(frame.data_length)) {
      set_detailed_error("Writing data length failed.");
      return false;
    }
  }

  if (frame.data_length == 0) {
    return true;
  }
  if (data_producer_ == nullptr) {
    if (!writer->WriteBytes(frame.data_buffer, frame.data_length)) {
      set_detailed_error("Writing frame data failed.");
      return false;
    }
  } else {
    QUICHE_DCHECK_EQ(nullptr, frame.data_buffer);

    if (data_producer_->WriteStreamData(frame.stream_id, frame.offset,
                                        frame.data_length,
                                        writer) != WRITE_SUCCESS) {
      set_detailed_error("Writing frame data from producer failed.");
      return false;
    }
  }
  return true;
}

bool QuicFramer::AppendCryptoFrame(const QuicCryptoFrame& frame,
                                   QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.offset))) {
    set_detailed_error("Writing data offset failed.");
    return false;
  }
  if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.data_length))) {
    set_detailed_error("Writing data length failed.");
    return false;
  }
  if (data_producer_ == nullptr) {
    if (frame.data_buffer == nullptr ||
        !writer->WriteBytes(frame.data_buffer, frame.data_length)) {
      set_detailed_error("Writing frame data failed.");
      return false;
    }
  } else {
    QUICHE_DCHECK_EQ(nullptr, frame.data_buffer);
    if (!data_producer_->WriteCryptoData(frame.level, frame.offset,
                                         frame.data_length, writer)) {
      set_detailed_error("Writing frame data from producer failed.");
      return false;
    }
  }
  return true;
}

bool QuicFramer::AppendAckFrequencyFrame(const QuicAckFrequencyFrame& frame,
                                         QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.sequence_number)) {
    set_detailed_error("Writing sequence number failed.");
    return false;
  }
  if (!writer->WriteVarInt62(frame.packet_tolerance)) {
    set_detailed_error("Writing packet tolerance failed.");
    return false;
  }
  if (!writer->WriteVarInt62(
          static_cast<uint64_t>(frame.max_ack_delay.ToMicroseconds()))) {
    set_detailed_error("Writing max_ack_delay_us failed.");
    return false;
  }
  if (!writer->WriteUInt8(static_cast<uint8_t>(frame.ignore_order))) {
    set_detailed_error("Writing ignore_order failed.");
    return false;
  }

  return true;
}

bool QuicFramer::AppendResetFrameAtFrame(const QuicResetStreamAtFrame& frame,
                                         QuicDataWriter& writer) {
  if (frame.reliable_offset > frame.final_offset) {
    QUIC_BUG(AppendResetFrameAtFrame_offset_mismatch)
        << "reliable_offset > final_offset";
    set_detailed_error("reliable_offset > final_offset");
    return false;
  }
  absl::Status status =
      quiche::SerializeIntoWriter(writer, quiche::WireVarInt62(frame.stream_id),
                                  quiche::WireVarInt62(frame.error),
                                  quiche::WireVarInt62(frame.final_offset),
                                  quiche::WireVarInt62(frame.reliable_offset));
  if (!status.ok()) {
    set_detailed_error(std::string(status.message()));
    return false;
  }
  return true;
}

void QuicFramer::set_version(const ParsedQuicVersion version) {
  QUICHE_DCHECK(IsSupportedVersion(version))
      << ParsedQuicVersionToString(version);
  version_ = version;
}

bool QuicFramer::AppendAckFrameAndTypeByte(const QuicAckFrame& frame,
                                           QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(transport_version())) {
    return AppendIetfAckFrameAndTypeByte(frame, writer);
  }

  const AckFrameInfo new_ack_info = GetAckFrameInfo(frame);
  QuicPacketNumber largest_acked = LargestAcked(frame);
  QuicPacketNumberLength largest_acked_length =
      GetMinPacketNumberLength(largest_acked);
  QuicPacketNumberLength ack_block_length =
      GetMinPacketNumberLength(QuicPacketNumber(new_ack_info.max_block_length));
  // Calculate available bytes for timestamps and ack blocks.
  int32_t available_timestamp_and_ack_block_bytes =
      writer->capacity() - writer->length() - ack_block_length -
      GetMinAckFrameSize(version_.transport_version, frame,
                         local_ack_delay_exponent_,
                         UseIetfAckWithReceiveTimestamp(frame)) -
      (new_ack_info.num_ack_blocks != 0 ? kNumberOfAckBlocksSize : 0);
  QUICHE_DCHECK_LE(0, available_timestamp_and_ack_block_bytes);

  uint8_t type_byte = 0;
  SetBit(&type_byte, new_ack_info.num_ack_blocks != 0,
         kQuicHasMultipleAckBlocksOffset);

  SetBits(&type_byte, GetPacketNumberFlags(largest_acked_length),
          kQuicSequenceNumberLengthNumBits, kLargestAckedOffset);

  SetBits(&type_byte, GetPacketNumberFlags(ack_block_length),
          kQuicSequenceNumberLengthNumBits, kActBlockLengthOffset);

  type_byte |= kQuicFrameTypeAckMask;

  if (!writer->WriteUInt8(type_byte)) {
    return false;
  }

  size_t max_num_ack_blocks = available_timestamp_and_ack_block_bytes /
                              (ack_block_length + PACKET_1BYTE_PACKET_NUMBER);

  // Number of ack blocks.
  size_t num_ack_blocks =
      std::min(new_ack_info.num_ack_blocks, max_num_ack_blocks);
  if (num_ack_blocks > std::numeric_limits<uint8_t>::max()) {
    num_ack_blocks = std::numeric_limits<uint8_t>::max();
  }

  // Largest acked.
  if (!AppendPacketNumber(largest_acked_length, largest_acked, writer)) {
    return false;
  }

  // Largest acked delta time.
  uint64_t ack_delay_time_us = kUFloat16MaxValue;
  if (!frame.ack_delay_time.IsInfinite()) {
    QUICHE_DCHECK_LE(0u, frame.ack_delay_time.ToMicroseconds());
    ack_delay_time_us = frame.ack_delay_time.ToMicroseconds();
  }
  if (!writer->WriteUFloat16(ack_delay_time_us)) {
    return false;
  }

  if (num_ack_blocks > 0) {
    if (!writer->WriteBytes(&num_ack_blocks, 1)) {
      return false;
    }
  }

  // First ack block length.
  if (!AppendPacketNumber(ack_block_length,
                          QuicPacketNumber(new_ack_info.first_block_length),
                          writer)) {
    return false;
  }

  // Ack blocks.
  if (num_ack_blocks > 0) {
    size_t num_ack_blocks_written = 0;
    // Append, in descending order from the largest ACKed packet, a series of
    // ACK blocks that represents the successfully acknoweldged packets. Each
    // appended gap/block length represents a descending delta from the previous
    // block. i.e.:
    // |--- length ---|--- gap ---|--- length ---|--- gap ---|--- largest ---|
    // For gaps larger than can be represented by a single encoded gap, a 0
    // length gap of the maximum is used, i.e.:
    // |--- length ---|--- gap ---|- 0 -|--- gap ---|--- largest ---|
    auto itr = frame.packets.rbegin();
    QuicPacketNumber previous_start = itr->min();
    ++itr;

    for (;
         itr != frame.packets.rend() && num_ack_blocks_written < num_ack_blocks;
         previous_start = itr->min(), ++itr) {
      const auto& interval = *itr;
      const uint64_t total_gap = previous_start - interval.max();
      const size_t num_encoded_gaps =
          (total_gap + std::numeric_limits<uint8_t>::max() - 1) /
          std::numeric_limits<uint8_t>::max();

      // Append empty ACK blocks because the gap is longer than a single gap.
      for (size_t i = 1;
           i < num_encoded_gaps && num_ack_blocks_written < num_ack_blocks;
           ++i) {
        if (!AppendAckBlock(std::numeric_limits<uint8_t>::max(),
                            ack_block_length, 0, writer)) {
          return false;
        }
        ++num_ack_blocks_written;
      }
      if (num_ack_blocks_written >= num_ack_blocks) {
        if (ABSL_PREDICT_FALSE(num_ack_blocks_written != num_ack_blocks)) {
          QUIC_BUG(quic_bug_10850_85)
              << "Wrote " << num_ack_blocks_written << ", expected to write "
              << num_ack_blocks;
        }
        break;
      }

      const uint8_t last_gap =
          total_gap -
          (num_encoded_gaps - 1) * std::numeric_limits<uint8_t>::max();
      // Append the final ACK block with a non-empty size.
      if (!AppendAckBlock(last_gap, ack_block_length, interval.Length(),
                          writer)) {
        return false;
      }
      ++num_ack_blocks_written;
    }
    QUICHE_DCHECK_EQ(num_ack_blocks, num_ack_blocks_written);
  }
  // Timestamps.
  // If we don't process timestamps or if we don't have enough available space
  // to append all the timestamps, don't append any of them.
  if (process_timestamps_ && writer->capacity() - writer->length() >=
                                 GetAckFrameTimeStampSize(frame)) {
    if (!AppendTimestampsToAckFrame(frame, writer)) {
      return false;
    }
  } else {
    uint8_t num_received_packets = 0;
    if (!writer->WriteBytes(&num_received_packets, 1)) {
      return false;
    }
  }

  return true;
}

bool QuicFramer::AppendTimestampsToAckFrame(const QuicAckFrame& frame,
                                            QuicDataWriter* writer) {
  QUICHE_DCHECK_GE(std::numeric_limits<uint8_t>::max(),
                   frame.received_packet_times.size());
  // num_received_packets is only 1 byte.
  if (frame.received_packet_times.size() >
      std::numeric_limits<uint8_t>::max()) {
    return false;
  }

  uint8_t num_received_packets = frame.received_packet_times.size();
  if (!writer->WriteBytes(&num_received_packets, 1)) {
    return false;
  }
  if (num_received_packets == 0) {
    return true;
  }

  auto it = frame.received_packet_times.begin();
  QuicPacketNumber packet_number = it->first;
  uint64_t delta_from_largest_observed = LargestAcked(frame) - packet_number;

  QUICHE_DCHECK_GE(std::numeric_limits<uint8_t>::max(),
                   delta_from_largest_observed);
  if (delta_from_largest_observed > std::numeric_limits<uint8_t>::max()) {
    return false;
  }

  if (!writer->WriteUInt8(delta_from_largest_observed)) {
    return false;
  }

  // Use the lowest 4 bytes of the time delta from the creation_time_.
  const uint64_t time_epoch_delta_us = UINT64_C(1) << 32;
  uint32_t time_delta_us =
      static_cast<uint32_t>((it->second - creation_time_).ToMicroseconds() &
                            (time_epoch_delta_us - 1));
  if (!writer->WriteUInt32(time_delta_us)) {
    return false;
  }

  QuicTime prev_time = it->second;

  for (++it; it != frame.received_packet_times.end(); ++it) {
    packet_number = it->first;
    delta_from_largest_observed = LargestAcked(frame) - packet_number;

    if (delta_from_largest_observed > std::numeric_limits<uint8_t>::max()) {
      return false;
    }

    if (!writer->WriteUInt8(delta_from_largest_observed)) {
      return false;
    }

    uint64_t frame_time_delta_us = (it->second - prev_time).ToMicroseconds();
    prev_time = it->second;
    if (!writer->WriteUFloat16(frame_time_delta_us)) {
      return false;
    }
  }
  return true;
}

absl::InlinedVector<QuicFramer::AckTimestampRange, 2>
QuicFramer::GetAckTimestampRanges(const QuicAckFrame& frame,
                                  std::string& detailed_error) const {
  detailed_error = "";
  if (frame.received_packet_times.empty()) {
    return {};
  }

  absl::InlinedVector<AckTimestampRange, 2> timestamp_ranges;

  for (size_t r = 0; r < std::min<size_t>(max_receive_timestamps_per_ack_,
                                          frame.received_packet_times.size());
       ++r) {
    const size_t i = frame.received_packet_times.size() - 1 - r;
    const QuicPacketNumber packet_number = frame.received_packet_times[i].first;
    const QuicTime receive_timestamp = frame.received_packet_times[i].second;

    if (timestamp_ranges.empty()) {
      if (receive_timestamp < creation_time_ ||
          LargestAcked(frame) < packet_number) {
        detailed_error =
            "The first packet is either received earlier than framer creation "
            "time, or larger than largest acked packet.";
        QUIC_BUG(quic_framer_ack_ts_first_packet_bad)
            << detailed_error << " receive_timestamp:" << receive_timestamp
            << ", framer_creation_time:" << creation_time_
            << ", packet_number:" << packet_number
            << ", largest_acked:" << LargestAcked(frame);
        return {};
      }
      timestamp_ranges.push_back(AckTimestampRange());
      timestamp_ranges.back().gap = LargestAcked(frame) - packet_number;
      timestamp_ranges.back().range_begin = i;
      timestamp_ranges.back().range_end = i;
      continue;
    }

    const size_t prev_i = timestamp_ranges.back().range_end;
    const QuicPacketNumber prev_packet_number =
        frame.received_packet_times[prev_i].first;
    const QuicTime prev_receive_timestamp =
        frame.received_packet_times[prev_i].second;

    QUIC_DVLOG(3) << "prev_packet_number:" << prev_packet_number
                  << ", packet_number:" << packet_number;
    if (prev_receive_timestamp < receive_timestamp ||
        prev_packet_number <= packet_number) {
      detailed_error = "Packet number and/or receive time not in order.";
      QUIC_BUG(quic_framer_ack_ts_packet_out_of_order)
          << detailed_error << " packet_number:" << packet_number
          << ", receive_timestamp:" << receive_timestamp
          << ", prev_packet_number:" << prev_packet_number
          << ", prev_receive_timestamp:" << prev_receive_timestamp;
      return {};
    }

    if (prev_packet_number == packet_number + 1) {
      timestamp_ranges.back().range_end = i;
    } else {
      timestamp_ranges.push_back(AckTimestampRange());
      timestamp_ranges.back().gap = prev_packet_number - 2 - packet_number;
      timestamp_ranges.back().range_begin = i;
      timestamp_ranges.back().range_end = i;
    }
  }

  return timestamp_ranges;
}

int64_t QuicFramer::FrameAckTimestampRanges(
    const QuicAckFrame& frame,
    const absl::InlinedVector<AckTimestampRange, 2>& timestamp_ranges,
    QuicDataWriter* writer) const {
  int64_t size = 0;
  auto maybe_write_var_int62 = [&](uint64_t value) {
    size += QuicDataWriter::GetVarInt62Len(value);
    if (writer != nullptr && !writer->WriteVarInt62(value)) {
      return false;
    }
    return true;
  };

  if (!maybe_write_var_int62(timestamp_ranges.size())) {
    return -1;
  }

  // |effective_prev_time| is the exponent-encoded timestamp of the previous
  // packet.
  std::optional<QuicTime> effective_prev_time;
  for (const AckTimestampRange& range : timestamp_ranges) {
    QUIC_DVLOG(3) << "Range: gap:" << range.gap << ", beg:" << range.range_begin
                  << ", end:" << range.range_end;
    if (!maybe_write_var_int62(range.gap)) {
      return -1;
    }

    if (!maybe_write_var_int62(range.range_begin - range.range_end + 1)) {
      return -1;
    }

    for (int64_t i = range.range_begin; i >= range.range_end; --i) {
      const QuicTime receive_timestamp = frame.received_packet_times[i].second;
      uint64_t time_delta;
      if (effective_prev_time.has_value()) {
        time_delta =
            (*effective_prev_time - receive_timestamp).ToMicroseconds();
        QUIC_DVLOG(3) << "time_delta:" << time_delta
                      << ", exponent:" << receive_timestamps_exponent_
                      << ", effective_prev_time:" << *effective_prev_time
                      << ", recv_time:" << receive_timestamp;
        time_delta = time_delta >> receive_timestamps_exponent_;
        effective_prev_time = *effective_prev_time -
                              QuicTime::Delta::FromMicroseconds(
                                  time_delta << receive_timestamps_exponent_);
      } else {
        // The first delta is from framer creation to the current receive
        // timestamp (forward in time), whereas in the common case subsequent
        // deltas move backwards in time.
        time_delta = (receive_timestamp - creation_time_).ToMicroseconds();
        QUIC_DVLOG(3) << "First time_delta:" << time_delta
                      << ", exponent:" << receive_timestamps_exponent_
                      << ", recv_time:" << receive_timestamp
                      << ", creation_time:" << creation_time_;
        // Round up the first exponent-encoded time delta so that the next
        // receive timestamp is guaranteed to be decreasing.
        time_delta = ((time_delta - 1) >> receive_timestamps_exponent_) + 1;
        effective_prev_time =
            creation_time_ + QuicTime::Delta::FromMicroseconds(
                                 time_delta << receive_timestamps_exponent_);
      }

      if (!maybe_write_var_int62(time_delta)) {
        return -1;
      }
    }
  }

  return size;
}

bool QuicFramer::AppendIetfTimestampsToAckFrame(const QuicAckFrame& frame,
                                                QuicDataWriter* writer) {
  QUICHE_DCHECK(!frame.received_packet_times.empty());
  std::string detailed_error;
  const absl::InlinedVector<AckTimestampRange, 2> timestamp_ranges =
      GetAckTimestampRanges(frame, detailed_error);
  if (!detailed_error.empty()) {
    set_detailed_error(std::move(detailed_error));
    return false;
  }

  // Compute the size first using a null writer.
  int64_t size =
      FrameAckTimestampRanges(frame, timestamp_ranges, /*writer=*/nullptr);
  if (size > static_cast<int64_t>(writer->capacity() - writer->length())) {
    QUIC_DVLOG(1) << "Insufficient room to write IETF ack receive timestamps. "
                     "size_remain:"
                  << (writer->capacity() - writer->length())
                  << ", size_needed:" << size;
    // Write a Timestamp Range Count of 0.
    return writer->WriteVarInt62(0);
  }

  return FrameAckTimestampRanges(frame, timestamp_ranges, writer) > 0;
}

bool QuicFramer::AppendIetfAckFrameAndTypeByte(const QuicAckFrame& frame,
                                               QuicDataWriter* writer) {
  uint8_t type = IETF_ACK;
  uint64_t ecn_size = 0;
  if (UseIetfAckWithReceiveTimestamp(frame)) {
    type = IETF_ACK_RECEIVE_TIMESTAMPS;
  } else if (frame.ecn_counters.has_value()) {
    // Change frame type to ACK_ECN if any ECN count is available.
    type = IETF_ACK_ECN;
    ecn_size = AckEcnCountSize(frame);
  }

  if (!writer->WriteVarInt62(type)) {
    set_detailed_error("No room for frame-type");
    return false;
  }

  QuicPacketNumber largest_acked = LargestAcked(frame);
  if (!writer->WriteVarInt62(largest_acked.ToUint64())) {
    set_detailed_error("No room for largest-acked in ack frame");
    return false;
  }

  uint64_t ack_delay_time_us = quiche::kVarInt62MaxValue;
  if (!frame.ack_delay_time.IsInfinite()) {
    QUICHE_DCHECK_LE(0u, frame.ack_delay_time.ToMicroseconds());
    ack_delay_time_us = frame.ack_delay_time.ToMicroseconds();
    ack_delay_time_us = ack_delay_time_us >> local_ack_delay_exponent_;
  }

  if (!writer->WriteVarInt62(ack_delay_time_us)) {
    set_detailed_error("No room for ack-delay in ack frame");
    return false;
  }

  if (frame.packets.Empty() || frame.packets.Max() != largest_acked) {
    QUIC_BUG(quic_bug_10850_88) << "Malformed ack frame: " << frame;
    set_detailed_error("Malformed ack frame");
    return false;
  }

  // Latch ack_block_count for potential truncation.
  const uint64_t ack_block_count = frame.packets.NumIntervals() - 1;
  QuicDataWriter count_writer(QuicDataWriter::GetVarInt62Len(ack_block_count),
                              writer->data() + writer->length());
  if (!writer->WriteVarInt62(ack_block_count)) {
    set_detailed_error("No room for ack block count in ack frame");
    return false;
  }
  auto iter = frame.packets.rbegin();
  if (!writer->WriteVarInt62(iter->Length() - 1)) {
    set_detailed_error("No room for first ack block in ack frame");
    return false;
  }
  QuicPacketNumber previous_smallest = iter->min();
  ++iter;
  // Append remaining ACK blocks.
  uint64_t appended_ack_blocks = 0;
  for (; iter != frame.packets.rend(); ++iter) {
    const uint64_t gap = previous_smallest - iter->max() - 1;
    const uint64_t ack_range = iter->Length() - 1;

    if (type == IETF_ACK_RECEIVE_TIMESTAMPS &&
        writer->remaining() <
            static_cast<size_t>(QuicDataWriter::GetVarInt62Len(gap) +
                                QuicDataWriter::GetVarInt62Len(ack_range) +
                                QuicDataWriter::GetVarInt62Len(0))) {
      // If we write this ACK range we won't have space for a timestamp range
      // count of 0.
      break;
    } else if (writer->remaining() < ecn_size ||
               writer->remaining() - ecn_size <
                   static_cast<size_t>(
                       QuicDataWriter::GetVarInt62Len(gap) +
                       QuicDataWriter::GetVarInt62Len(ack_range))) {
      // ACK range does not fit, truncate it.
      break;
    }
    const bool success =
        writer->WriteVarInt62(gap) && writer->WriteVarInt62(ack_range);
    QUICHE_DCHECK(success);
    previous_smallest = iter->min();
    ++appended_ack_blocks;
  }

  if (appended_ack_blocks < ack_block_count) {
    // Truncation is needed, rewrite the ack block count.
    if (QuicDataWriter::GetVarInt62Len(appended_ack_blocks) !=
            QuicDataWriter::GetVarInt62Len(ack_block_count) ||
        !count_writer.WriteVarInt62(appended_ack_blocks)) {
      // This should never happen as ack_block_count is limited by
      // max_ack_ranges_.
      QUIC_BUG(quic_bug_10850_89)
          << "Ack frame truncation fails. ack_block_count: " << ack_block_count
          << ", appended count: " << appended_ack_blocks;
      set_detailed_error("ACK frame truncation fails");
      return false;
    }
    QUIC_DLOG(INFO) << ENDPOINT << "ACK ranges get truncated from "
                    << ack_block_count << " to " << appended_ack_blocks;
  }

  if (type == IETF_ACK_ECN) {
    // Encode the ECN counts.
    if (!writer->WriteVarInt62(frame.ecn_counters->ect0)) {
      set_detailed_error("No room for ect_0_count in ack frame");
      return false;
    }
    if (!writer->WriteVarInt62(frame.ecn_counters->ect1)) {
      set_detailed_error("No room for ect_1_count in ack frame");
      return false;
    }
    if (!writer->WriteVarInt62(frame.ecn_counters->ce)) {
      set_detailed_error("No room for ecn_ce_count in ack frame");
      return false;
    }
  }

  if (type == IETF_ACK_RECEIVE_TIMESTAMPS) {
    if (!AppendIetfTimestampsToAckFrame(frame, writer)) {
      return false;
    }
  }

  return true;
}

bool QuicFramer::AppendRstStreamFrame(const QuicRstStreamFrame& frame,
                                      QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return AppendIetfResetStreamFrame(frame, writer);
  }
  if (!writer->WriteUInt32(frame.stream_id)) {
    return false;
  }

  if (!writer->WriteUInt64(frame.byte_offset)) {
    return false;
  }

  uint32_t error_code = static_cast<uint32_t>(frame.error_code);
  if (!writer->WriteUInt32(error_code)) {
    return false;
  }

  return true;
}

bool QuicFramer::AppendConnectionCloseFrame(
    const QuicConnectionCloseFrame& frame, QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return AppendIetfConnectionCloseFrame(frame, writer);
  }
  uint32_t error_code = static_cast<uint32_t>(frame.wire_error_code);
  if (!writer->WriteUInt32(error_code)) {
    return false;
  }
  if (!writer->WriteStringPiece16(TruncateErrorString(frame.error_details))) {
    return false;
  }
  return true;
}

bool QuicFramer::AppendGoAwayFrame(const QuicGoAwayFrame& frame,
                                   QuicDataWriter* writer) {
  uint32_t error_code = static_cast<uint32_t>(frame.error_code);
  if (!writer->WriteUInt32(error_code)) {
    return false;
  }
  uint32_t stream_id = static_cast<uint32_t>(frame.last_good_stream_id);
  if (!writer->WriteUInt32(stream_id)) {
    return false;
  }
  if (!writer->WriteStringPiece16(TruncateErrorString(frame.reason_phrase))) {
    return false;
  }
  return true;
}

bool QuicFramer::AppendWindowUpdateFrame(const QuicWindowUpdateFrame& frame,
                                         QuicDataWriter* writer) {
  uint32_t stream_id = static_cast<uint32_t>(frame.stream_id);
  if (!writer->WriteUInt32(stream_id)) {
    return false;
  }
  if (!writer->WriteUInt64(frame.max_data)) {
    return false;
  }
  return true;
}

bool QuicFramer::AppendBlockedFrame(const QuicBlockedFrame& frame,
                                    QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    if (frame.stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
      return AppendDataBlockedFrame(frame, writer);
    }
    return AppendStreamDataBlockedFrame(frame, writer);
  }
  uint32_t stream_id = static_cast<uint32_t>(frame.stream_id);
  if (!writer->WriteUInt32(stream_id)) {
    return false;
  }
  return true;
}

bool QuicFramer::AppendPaddingFrame(const QuicPaddingFrame& frame,
                                    QuicDataWriter* writer) {
  if (frame.num_padding_bytes == 0) {
    return false;
  }
  if (frame.num_padding_bytes < 0) {
    QUIC_BUG_IF(quic_bug_12975_9, frame.num_padding_bytes != -1);
    writer->WritePadding();
    return true;
  }
  // Please note, num_padding_bytes includes type byte which has been written.
  return writer->WritePaddingBytes(frame.num_padding_bytes - 1);
}

bool QuicFramer::AppendMessageFrameAndTypeByte(const QuicMessageFrame& frame,
                                               bool last_frame_in_packet,
                                               QuicDataWriter* writer) {
  uint8_t type_byte;
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    type_byte = last_frame_in_packet ? IETF_EXTENSION_MESSAGE_NO_LENGTH_V99
                                     : IETF_EXTENSION_MESSAGE_V99;
  } else {
    QUIC_CODE_COUNT(quic_legacy_message_frame_codepoint_write);
    type_byte = last_frame_in_packet ? IETF_EXTENSION_MESSAGE_NO_LENGTH
                                     : IETF_EXTENSION_MESSAGE;
  }
  if (!writer->WriteUInt8(type_byte)) {
    return false;
  }
  if (!last_frame_in_packet && !writer->WriteVarInt62(frame.message_length)) {
    return false;
  }
  for (const auto& slice : frame.message_data) {
    if (!writer->WriteBytes(slice.data(), slice.length())) {
      return false;
    }
  }
  return true;
}

bool QuicFramer::RaiseError(QuicErrorCode error) {
  QUIC_DLOG(INFO) << ENDPOINT << "Error: " << QuicErrorCodeToString(error)
                  << " detail: " << detailed_error_;
  set_error(error);
  if (visitor_) {
    visitor_->OnError(this);
  }
  return false;
}

bool QuicFramer::IsVersionNegotiation(const QuicPacketHeader& header) const {
  return header.form == IETF_QUIC_LONG_HEADER_PACKET &&
         header.long_packet_type == VERSION_NEGOTIATION;
}

bool QuicFramer::AppendIetfConnectionCloseFrame(
    const QuicConnectionCloseFrame& frame, QuicDataWriter* writer) {
  if (frame.close_type != IETF_QUIC_TRANSPORT_CONNECTION_CLOSE &&
      frame.close_type != IETF_QUIC_APPLICATION_CONNECTION_CLOSE) {
    QUIC_BUG(quic_bug_10850_90)
        << "Invalid close_type for writing IETF CONNECTION CLOSE.";
    set_detailed_error("Invalid close_type for writing IETF CONNECTION CLOSE.");
    return false;
  }

  if (!writer->WriteVarInt62(frame.wire_error_code)) {
    set_detailed_error("Can not write connection close frame error code");
    return false;
  }

  if (frame.close_type == IETF_QUIC_TRANSPORT_CONNECTION_CLOSE) {
    // Write the frame-type of the frame causing the error only
    // if it's a CONNECTION_CLOSE/Transport.
    if (!writer->WriteVarInt62(frame.transport_close_frame_type)) {
      set_detailed_error("Writing frame type failed.");
      return false;
    }
  }

  // There may be additional error information available in the extracted error
  // code. Encode the error information in the reason phrase and serialize the
  // result.
  std::string final_error_string =
      GenerateErrorString(frame.error_details, frame.quic_error_code);
  if (!writer->WriteStringPieceVarInt62(
          TruncateErrorString(final_error_string))) {
    set_detailed_error("Can not write connection close phrase");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessIetfConnectionCloseFrame(
    QuicDataReader* reader, QuicConnectionCloseType type,
    QuicConnectionCloseFrame* frame) {
  frame->close_type = type;

  uint64_t error_code;
  if (!reader->ReadVarInt62(&error_code)) {
    set_detailed_error("Unable to read connection close error code.");
    return false;
  }

  frame->wire_error_code = error_code;

  if (type == IETF_QUIC_TRANSPORT_CONNECTION_CLOSE) {
    // The frame-type of the frame causing the error is present only
    // if it's a CONNECTION_CLOSE/Transport.
    if (!reader->ReadVarInt62(&frame->transport_close_frame_type)) {
      set_detailed_error("Unable to read connection close frame type.");
      return false;
    }
  }

  uint64_t phrase_length;
  if (!reader->ReadVarInt62(&phrase_length)) {
    set_detailed_error("Unable to read connection close error details.");
    return false;
  }

  absl::string_view phrase;
  if (!reader->ReadStringPiece(&phrase, static_cast<size_t>(phrase_length))) {
    set_detailed_error("Unable to read connection close error details.");
    return false;
  }
  frame->error_details = std::string(phrase);

  // The frame may have an extracted error code in it. Look for it and
  // extract it. If it's not present, MaybeExtract will return
  // QUIC_IETF_GQUIC_ERROR_MISSING.
  MaybeExtractQuicErrorCode(frame);
  return true;
}

// IETF Quic Path Challenge/Response frames.
bool QuicFramer::ProcessPathChallengeFrame(QuicDataReader* reader,
                                           QuicPathChallengeFrame* frame) {
  if (!reader->ReadBytes(frame->data_buffer.data(),
                         frame->data_buffer.size())) {
    set_detailed_error("Can not read path challenge data.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessPathResponseFrame(QuicDataReader* reader,
                                          QuicPathResponseFrame* frame) {
  if (!reader->ReadBytes(frame->data_buffer.data(),
                         frame->data_buffer.size())) {
    set_detailed_error("Can not read path response data.");
    return false;
  }
  return true;
}

bool QuicFramer::AppendPathChallengeFrame(const QuicPathChallengeFrame& frame,
                                          QuicDataWriter* writer) {
  if (!writer->WriteBytes(frame.data_buffer.data(), frame.data_buffer.size())) {
    set_detailed_error("Writing Path Challenge data failed.");
    return false;
  }
  return true;
}

bool QuicFramer::AppendPathResponseFrame(const QuicPathResponseFrame& frame,
                                         QuicDataWriter* writer) {
  if (!writer->WriteBytes(frame.data_buffer.data(), frame.data_buffer.size())) {
    set_detailed_error("Writing Path Response data failed.");
    return false;
  }
  return true;
}

// Add a new ietf-format stream reset frame.
// General format is
//    stream id
//    application error code
//    final offset
bool QuicFramer::AppendIetfResetStreamFrame(const QuicRstStreamFrame& frame,
                                            QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.stream_id))) {
    set_detailed_error("Writing reset-stream stream id failed.");
    return false;
  }
  if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.ietf_error_code))) {
    set_detailed_error("Writing reset-stream error code failed.");
    return false;
  }
  if (!writer->WriteVarInt62(static_cast<uint64_t>(frame.byte_offset))) {
    set_detailed_error("Writing reset-stream final-offset failed.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessIetfResetStreamFrame(QuicDataReader* reader,
                                             QuicRstStreamFrame* frame) {
  // Get Stream ID from frame. ReadVarIntStreamID returns false
  // if either A) there is a read error or B) the resulting value of
  // the Stream ID is larger than the maximum allowed value.
  if (!ReadUint32FromVarint62(reader, IETF_RST_STREAM, &frame->stream_id)) {
    return false;
  }

  if (!reader->ReadVarInt62(&frame->ietf_error_code)) {
    set_detailed_error("Unable to read rst stream error code.");
    return false;
  }

  frame->error_code =
      IetfResetStreamErrorCodeToRstStreamErrorCode(frame->ietf_error_code);

  if (!reader->ReadVarInt62(&frame->byte_offset)) {
    set_detailed_error("Unable to read rst stream sent byte offset.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessStopSendingFrame(
    QuicDataReader* reader, QuicStopSendingFrame* stop_sending_frame) {
  if (!ReadUint32FromVarint62(reader, IETF_STOP_SENDING,
                              &stop_sending_frame->stream_id)) {
    return false;
  }

  if (!reader->ReadVarInt62(&stop_sending_frame->ietf_error_code)) {
    set_detailed_error("Unable to read stop sending application error code.");
    return false;
  }

  stop_sending_frame->error_code = IetfResetStreamErrorCodeToRstStreamErrorCode(
      stop_sending_frame->ietf_error_code);
  return true;
}

bool QuicFramer::AppendStopSendingFrame(
    const QuicStopSendingFrame& stop_sending_frame, QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(stop_sending_frame.stream_id)) {
    set_detailed_error("Can not write stop sending stream id");
    return false;
  }
  if (!writer->WriteVarInt62(
          static_cast<uint64_t>(stop_sending_frame.ietf_error_code))) {
    set_detailed_error("Can not write application error code");
    return false;
  }
  return true;
}

// Append/process IETF-Format MAX_DATA Frame
bool QuicFramer::AppendMaxDataFrame(const QuicWindowUpdateFrame& frame,
                                    QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.max_data)) {
    set_detailed_error("Can not write MAX_DATA byte-offset");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessMaxDataFrame(QuicDataReader* reader,
                                     QuicWindowUpdateFrame* frame) {
  frame->stream_id = QuicUtils::GetInvalidStreamId(transport_version());
  if (!reader->ReadVarInt62(&frame->max_data)) {
    set_detailed_error("Can not read MAX_DATA byte-offset");
    return false;
  }
  return true;
}

// Append/process IETF-Format MAX_STREAM_DATA Frame
bool QuicFramer::AppendMaxStreamDataFrame(const QuicWindowUpdateFrame& frame,
                                          QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.stream_id)) {
    set_detailed_error("Can not write MAX_STREAM_DATA stream id");
    return false;
  }
  if (!writer->WriteVarInt62(frame.max_data)) {
    set_detailed_error("Can not write MAX_STREAM_DATA byte-offset");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessMaxStreamDataFrame(QuicDataReader* reader,
                                           QuicWindowUpdateFrame* frame) {
  if (!ReadUint32FromVarint62(reader, IETF_MAX_STREAM_DATA,
                              &frame->stream_id)) {
    return false;
  }
  if (!reader->ReadVarInt62(&frame->max_data)) {
    set_detailed_error("Can not read MAX_STREAM_DATA byte-count");
    return false;
  }
  return true;
}

bool QuicFramer::AppendMaxStreamsFrame(const QuicMaxStreamsFrame& frame,
                                       QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.stream_count)) {
    set_detailed_error("Can not write MAX_STREAMS stream count");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessMaxStreamsFrame(QuicDataReader* reader,
                                        QuicMaxStreamsFrame* frame,
                                        uint64_t frame_type) {
  if (!ReadUint32FromVarint62(reader,
                              static_cast<QuicIetfFrameType>(frame_type),
                              &frame->stream_count)) {
    return false;
  }
  frame->unidirectional = (frame_type == IETF_MAX_STREAMS_UNIDIRECTIONAL);
  return true;
}

bool QuicFramer::AppendDataBlockedFrame(const QuicBlockedFrame& frame,
                                        QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.offset)) {
    set_detailed_error("Can not write blocked offset.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessDataBlockedFrame(QuicDataReader* reader,
                                         QuicBlockedFrame* frame) {
  // Indicates that it is a BLOCKED frame (as opposed to STREAM_BLOCKED).
  frame->stream_id = QuicUtils::GetInvalidStreamId(transport_version());
  if (!reader->ReadVarInt62(&frame->offset)) {
    set_detailed_error("Can not read blocked offset.");
    return false;
  }
  return true;
}

bool QuicFramer::AppendStreamDataBlockedFrame(const QuicBlockedFrame& frame,
                                              QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.stream_id)) {
    set_detailed_error("Can not write stream blocked stream id.");
    return false;
  }
  if (!writer->WriteVarInt62(frame.offset)) {
    set_detailed_error("Can not write stream blocked offset.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessStreamDataBlockedFrame(QuicDataReader* reader,
                                               QuicBlockedFrame* frame) {
  if (!ReadUint32FromVarint62(reader, IETF_STREAM_DATA_BLOCKED,
                              &frame->stream_id)) {
    return false;
  }
  if (!reader->ReadVarInt62(&frame->offset)) {
    set_detailed_error("Can not read stream blocked offset.");
    return false;
  }
  return true;
}

bool QuicFramer::AppendStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame,
                                           QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.stream_count)) {
    set_detailed_error("Can not write STREAMS_BLOCKED stream count");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessStreamsBlockedFrame(QuicDataReader* reader,
                                            QuicStreamsBlockedFrame* frame,
                                            uint64_t frame_type) {
  if (!ReadUint32FromVarint62(reader,
                              static_cast<QuicIetfFrameType>(frame_type),
                              &frame->stream_count)) {
    return false;
  }
  if (frame->stream_count > QuicUtils::GetMaxStreamCount()) {
    // If stream count is such that the resulting stream ID would exceed our
    // implementation limit, generate an error.
    set_detailed_error(
        "STREAMS_BLOCKED stream count exceeds implementation limit.");
    return false;
  }
  frame->unidirectional = (frame_type == IETF_STREAMS_BLOCKED_UNIDIRECTIONAL);
  return true;
}

bool QuicFramer::AppendNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& frame, QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.sequence_number)) {
    set_detailed_error("Can not write New Connection ID sequence number");
    return false;
  }
  if (!writer->WriteVarInt62(frame.retire_prior_to)) {
    set_detailed_error("Can not write New Connection ID retire_prior_to");
    return false;
  }
  if (!writer->WriteLengthPrefixedConnectionId(frame.connection_id)) {
    set_detailed_error("Can not write New Connection ID frame connection ID");
    return false;
  }

  if (!writer->WriteBytes(
          static_cast<const void*>(&frame.stateless_reset_token),
          sizeof(frame.stateless_reset_token))) {
    set_detailed_error("Can not write New Connection ID Reset Token");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessNewConnectionIdFrame(QuicDataReader* reader,
                                             QuicNewConnectionIdFrame* frame) {
  if (!reader->ReadVarInt62(&frame->sequence_number)) {
    set_detailed_error(
        "Unable to read new connection ID frame sequence number.");
    return false;
  }

  if (!reader->ReadVarInt62(&frame->retire_prior_to)) {
    set_detailed_error(
        "Unable to read new connection ID frame retire_prior_to.");
    return false;
  }
  if (frame->retire_prior_to > frame->sequence_number) {
    set_detailed_error("Retire_prior_to > sequence_number.");
    return false;
  }

  if (!reader->ReadLengthPrefixedConnectionId(&frame->connection_id)) {
    set_detailed_error("Unable to read new connection ID frame connection id.");
    return false;
  }

  if (!QuicUtils::IsConnectionIdValidForVersion(frame->connection_id,
                                                transport_version())) {
    set_detailed_error("Invalid new connection ID length for version.");
    return false;
  }

  if (!reader->ReadBytes(&frame->stateless_reset_token,
                         sizeof(frame->stateless_reset_token))) {
    set_detailed_error("Can not read new connection ID frame reset token.");
    return false;
  }
  return true;
}

bool QuicFramer::AppendRetireConnectionIdFrame(
    const QuicRetireConnectionIdFrame& frame, QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.sequence_number)) {
    set_detailed_error("Can not write Retire Connection ID sequence number");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessRetireConnectionIdFrame(
    QuicDataReader* reader, QuicRetireConnectionIdFrame* frame) {
  if (!reader->ReadVarInt62(&frame->sequence_number)) {
    set_detailed_error(
        "Unable to read retire connection ID frame sequence number.");
    return false;
  }
  return true;
}

bool QuicFramer::ReadUint32FromVarint62(QuicDataReader* reader,
                                        QuicIetfFrameType type,
                                        QuicStreamId* id) {
  uint64_t temp_uint64;
  if (!reader->ReadVarInt62(&temp_uint64)) {
    set_detailed_error("Unable to read " + QuicIetfFrameTypeString(type) +
                       " frame stream id/count.");
    return false;
  }
  if (temp_uint64 > kMaxQuicStreamId) {
    set_detailed_error("Stream id/count of " + QuicIetfFrameTypeString(type) +
                       "frame is too large.");
    return false;
  }
  *id = static_cast<uint32_t>(temp_uint64);
  return true;
}

uint8_t QuicFramer::GetStreamFrameTypeByte(const QuicStreamFrame& frame,
                                           bool last_frame_in_packet) const {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return GetIetfStreamFrameTypeByte(frame, last_frame_in_packet);
  }
  uint8_t type_byte = 0;
  // Fin bit.
  type_byte |= frame.fin ? kQuicStreamFinMask : 0;

  // Data Length bit.
  type_byte <<= kQuicStreamDataLengthShift;
  type_byte |= last_frame_in_packet ? 0 : kQuicStreamDataLengthMask;

  // Offset 3 bits.
  type_byte <<= kQuicStreamShift;
  const size_t offset_len = GetStreamOffsetSize(frame.offset);
  if (offset_len > 0) {
    type_byte |= offset_len - 1;
  }

  // stream id 2 bits.
  type_byte <<= kQuicStreamIdShift;
  type_byte |= GetStreamIdSize(frame.stream_id) - 1;
  type_byte |= kQuicFrameTypeStreamMask;  // Set Stream Frame Type to 1.

  return type_byte;
}

uint8_t QuicFramer::GetIetfStreamFrameTypeByte(
    const QuicStreamFrame& frame, bool last_frame_in_packet) const {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(version_.transport_version));
  uint8_t type_byte = IETF_STREAM;
  if (!last_frame_in_packet) {
    type_byte |= IETF_STREAM_FRAME_LEN_BIT;
  }
  if (frame.offset != 0) {
    type_byte |= IETF_STREAM_FRAME_OFF_BIT;
  }
  if (frame.fin) {
    type_byte |= IETF_STREAM_FRAME_FIN_BIT;
  }
  return type_byte;
}

void QuicFramer::EnableMultiplePacketNumberSpacesSupport() {
  if (supports_multiple_packet_number_spaces_) {
    QUIC_BUG(quic_bug_10850_91)
        << "Multiple packet number spaces has already been enabled";
    return;
  }
  if (largest_packet_number_.IsInitialized()) {
    QUIC_BUG(quic_bug_10850_92)
        << "Try to enable multiple packet number spaces support after any "
           "packet has been received.";
    return;
  }

  supports_multiple_packet_number_spaces_ = true;
}

// static
QuicErrorCode QuicFramer::ParsePublicHeaderDispatcher(
    const QuicEncryptedPacket& packet,
    uint8_t expected_destination_connection_id_length,
    PacketHeaderFormat* format, QuicLongHeaderType* long_packet_type,
    bool* version_present, bool* has_length_prefix,
    QuicVersionLabel* version_label, ParsedQuicVersion* parsed_version,
    QuicConnectionId* destination_connection_id,
    QuicConnectionId* source_connection_id,
    std::optional<absl::string_view>* retry_token,
    std::string* detailed_error) {
  QuicDataReader reader(packet.data(), packet.length());
  if (reader.IsDoneReading()) {
    *detailed_error = "Unable to read first byte.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  const uint8_t first_byte = reader.PeekByte();
  if ((first_byte & FLAGS_LONG_HEADER) == 0 &&
      (first_byte & FLAGS_FIXED_BIT) == 0 &&
      (first_byte & FLAGS_DEMULTIPLEXING_BIT) == 0) {
    // All versions of Google QUIC up to and including Q043 set
    // FLAGS_DEMULTIPLEXING_BIT to one on all client-to-server packets. Q044
    // and Q045 were never default-enabled in production. All subsequent
    // versions of Google QUIC (starting with Q046) require FLAGS_FIXED_BIT to
    // be set to one on all packets. All versions of IETF QUIC (since
    // draft-ietf-quic-transport-17 which was earlier than the first IETF QUIC
    // version that was deployed in production by any implementation) also
    // require FLAGS_FIXED_BIT to be set to one on all packets. If a packet
    // has the FLAGS_LONG_HEADER bit set to one, it could be a first flight
    // from an unknown future version that allows the other two bits to be set
    // to zero. Based on this, packets that have all three of those bits set
    // to zero are known to be invalid.
    *detailed_error = "Invalid flags.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  const bool ietf_format = QuicUtils::IsIetfPacketHeader(first_byte);
  uint8_t unused_first_byte;
  quiche::QuicheVariableLengthIntegerLength retry_token_length_length;
  absl::string_view maybe_retry_token;
  QuicErrorCode error_code = ParsePublicHeader(
      &reader, expected_destination_connection_id_length, ietf_format,
      &unused_first_byte, format, version_present, has_length_prefix,
      version_label, parsed_version, destination_connection_id,
      source_connection_id, long_packet_type, &retry_token_length_length,
      &maybe_retry_token, detailed_error);
  if (retry_token_length_length != quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0) {
    *retry_token = maybe_retry_token;
  } else {
    retry_token->reset();
  }
  return error_code;
}

// static
QuicErrorCode QuicFramer::ParsePublicHeaderDispatcherShortHeaderLengthUnknown(
    const QuicEncryptedPacket& packet, PacketHeaderFormat* format,
    QuicLongHeaderType* long_packet_type, bool* version_present,
    bool* has_length_prefix, QuicVersionLabel* version_label,
    ParsedQuicVersion* parsed_version,
    QuicConnectionId* destination_connection_id,
    QuicConnectionId* source_connection_id,
    std::optional<absl::string_view>* retry_token, std::string* detailed_error,
    ConnectionIdGeneratorInterface& generator) {
  QuicDataReader reader(packet.data(), packet.length());
  // Get the first two bytes.
  if (reader.BytesRemaining() < 2) {
    *detailed_error = "Unable to read first two bytes.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  uint8_t two_bytes[2];
  reader.ReadBytes(two_bytes, 2);
  uint8_t expected_destination_connection_id_length =
      (!QuicUtils::IsIetfPacketHeader(two_bytes[0]) ||
       two_bytes[0] & FLAGS_LONG_HEADER)
          ? 0
          : generator.ConnectionIdLength(two_bytes[1]);
  return ParsePublicHeaderDispatcher(
      packet, expected_destination_connection_id_length, format,
      long_packet_type, version_present, has_length_prefix, version_label,
      parsed_version, destination_connection_id, source_connection_id,
      retry_token, detailed_error);
}

QuicErrorCode QuicFramer::TryDecryptInitialPacketDispatcher(
    const QuicEncryptedPacket& packet, const ParsedQuicVersion& version,
    PacketHeaderFormat format, QuicLongHeaderType long_packet_type,
    const QuicConnectionId& destination_connection_id,
    const QuicConnectionId& source_connection_id,
    const std::optional<absl::string_view>& retry_token,
    QuicPacketNumber largest_decrypted_inital_packet_number,
    QuicDecrypter& decrypter, std::optional<uint64_t>* packet_number) {
  QUICHE_DCHECK(packet_number != nullptr);
  packet_number->reset();

  if (packet.length() == 0 || format != IETF_QUIC_LONG_HEADER_PACKET ||
      !VersionHasIetfQuicFrames(version.transport_version) ||
      long_packet_type != INITIAL) {
    return QUIC_NO_ERROR;
  }

  QuicPacketHeader header;
  header.destination_connection_id = destination_connection_id;
  header.destination_connection_id_included =
      destination_connection_id.IsEmpty() ? CONNECTION_ID_ABSENT
                                          : CONNECTION_ID_PRESENT;
  header.source_connection_id = source_connection_id;
  header.source_connection_id_included = source_connection_id.IsEmpty()
                                             ? CONNECTION_ID_ABSENT
                                             : CONNECTION_ID_PRESENT;
  header.reset_flag = false;
  header.version_flag = true;
  header.has_possible_stateless_reset_token = false;
  header.type_byte = packet.data()[0];
  header.version = version;
  header.form = IETF_QUIC_LONG_HEADER_PACKET;
  header.long_packet_type = INITIAL;
  header.nonce = nullptr;
  header.retry_token = retry_token.value_or(absl::string_view());
  header.retry_token_length_length =
      QuicDataWriter::GetVarInt62Len(header.retry_token.length());

  // In a initial packet, the 3 fields after the Retry Token are:
  // - Packet Length (i)
  // - Packet Number (8..32)
  // - Packet Payload (8..)
  // Normally, GetStartOfEncryptedData returns the offset of the payload, here
  // we want the QuicDataReader to start reading from the packet length, so we
  // - Pass a length_length of VARIABLE_LENGTH_INTEGER_LENGTH_0,
  // - Pass a packet number length of PACKET_1BYTE_PACKET_NUMBER,
  // - Subtract PACKET_1BYTE_PACKET_NUMBER from the return value of
  //   GetStartOfEncryptedData.
  header.length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
  // The real header.packet_number_length is populated after a successful return
  // from RemoveHeaderProtection.
  header.packet_number_length = PACKET_1BYTE_PACKET_NUMBER;

  size_t remaining_packet_length_offset =
      GetStartOfEncryptedData(version.transport_version, header) -
      header.packet_number_length;
  if (packet.length() <= remaining_packet_length_offset) {
    return QUIC_INVALID_PACKET_HEADER;
  }
  QuicDataReader reader(packet.data() + remaining_packet_length_offset,
                        packet.length() - remaining_packet_length_offset);

  if (!reader.ReadVarInt62(&header.remaining_packet_length) ||
      // If |packet| is coalesced, truncate such that |reader| only sees the
      // first QUIC packet.
      !reader.TruncateRemaining(header.remaining_packet_length)) {
    return QUIC_INVALID_PACKET_HEADER;
  }

  header.length_length =
      QuicDataWriter::GetVarInt62Len(header.remaining_packet_length);

  AssociatedDataStorage associated_data;
  uint64_t full_packet_number;
  if (!RemoveHeaderProtection(&reader, packet, decrypter,
                              Perspective::IS_SERVER, version,
                              largest_decrypted_inital_packet_number, &header,
                              &full_packet_number, associated_data)) {
    return QUIC_INVALID_PACKET_HEADER;
  }

  ABSL_CACHELINE_ALIGNED char stack_buffer[kMaxIncomingPacketSize];
  std::unique_ptr<char[]> heap_buffer;
  char* decrypted_buffer;
  size_t decrypted_buffer_length;
  if (packet.length() <= kMaxIncomingPacketSize) {
    decrypted_buffer = stack_buffer;
    decrypted_buffer_length = kMaxIncomingPacketSize;
  } else {
    heap_buffer = std::make_unique<char[]>(packet.length());
    decrypted_buffer = heap_buffer.get();
    decrypted_buffer_length = packet.length();
  }

  size_t decrypted_length = 0;
  if (!decrypter.DecryptPacket(
          full_packet_number,
          absl::string_view(associated_data.data(), associated_data.size()),
          reader.ReadRemainingPayload(), decrypted_buffer, &decrypted_length,
          decrypted_buffer_length)) {
    return QUIC_DECRYPTION_FAILURE;
  }

  (*packet_number) = full_packet_number;
  return QUIC_NO_ERROR;
}

// static
QuicErrorCode QuicFramer::ParsePublicHeaderGoogleQuic(
    QuicDataReader* reader, uint8_t* first_byte, PacketHeaderFormat* format,
    bool* version_present, QuicVersionLabel* version_label,
    ParsedQuicVersion* parsed_version,
    QuicConnectionId* destination_connection_id, std::string* detailed_error) {
  *format = GOOGLE_QUIC_PACKET;
  *version_present = (*first_byte & PACKET_PUBLIC_FLAGS_VERSION) != 0;
  uint8_t destination_connection_id_length = 0;
  if ((*first_byte & PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID) != 0) {
    destination_connection_id_length = kQuicDefaultConnectionIdLength;
  }
  if (!reader->ReadConnectionId(destination_connection_id,
                                destination_connection_id_length)) {
    *detailed_error = "Unable to read ConnectionId.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  if (*version_present) {
    if (!ProcessVersionLabel(reader, version_label)) {
      *detailed_error = "Unable to read protocol version.";
      return QUIC_INVALID_PACKET_HEADER;
    }
    *parsed_version = ParseQuicVersionLabel(*version_label);
  }
  return QUIC_NO_ERROR;
}

namespace {

const QuicVersionLabel kProxVersionLabel = 0x50524F58;  // "PROX"

inline bool PacketHasLengthPrefixedConnectionIds(
    const QuicDataReader& reader, ParsedQuicVersion parsed_version,
    QuicVersionLabel version_label, uint8_t first_byte) {
  if (parsed_version.IsKnown()) {
    return parsed_version.HasLengthPrefixedConnectionIds();
  }

  // Received unsupported version, check known old unsupported versions.
  if (QuicVersionLabelUses4BitConnectionIdLength(version_label)) {
    return false;
  }

  // Received unknown version, check connection ID length byte.
  if (reader.IsDoneReading()) {
    // This check is required to safely peek the connection ID length byte.
    return true;
  }
  const uint8_t connection_id_length_byte = reader.PeekByte();

  // Check for packets produced by older versions of
  // QuicFramer::WriteClientVersionNegotiationProbePacket
  if (first_byte == 0xc0 && (connection_id_length_byte & 0x0f) == 0 &&
      connection_id_length_byte >= 0x50 && version_label == 0xcabadaba) {
    return false;
  }

  // Check for munged packets with version tag PROX.
  if ((connection_id_length_byte & 0x0f) == 0 &&
      connection_id_length_byte >= 0x20 && version_label == kProxVersionLabel) {
    return false;
  }

  return true;
}

inline bool ParseLongHeaderConnectionIds(
    QuicDataReader& reader, bool has_length_prefix,
    QuicVersionLabel version_label, QuicConnectionId& destination_connection_id,
    QuicConnectionId& source_connection_id, std::string& detailed_error) {
  if (has_length_prefix) {
    if (!reader.ReadLengthPrefixedConnectionId(&destination_connection_id)) {
      detailed_error = "Unable to read destination connection ID.";
      return false;
    }
    if (!reader.ReadLengthPrefixedConnectionId(&source_connection_id)) {
      if (version_label == kProxVersionLabel) {
        // The "PROX" version does not follow the length-prefixed invariants,
        // and can therefore attempt to read a payload byte and interpret it
        // as the source connection ID length, which could fail to parse.
        // In that scenario we keep the source connection ID empty but mark
        // parsing as successful.
        return true;
      }
      detailed_error = "Unable to read source connection ID.";
      return false;
    }
  } else {
    // Parse connection ID lengths.
    uint8_t connection_id_lengths_byte;
    if (!reader.ReadUInt8(&connection_id_lengths_byte)) {
      detailed_error = "Unable to read connection ID lengths.";
      return false;
    }
    uint8_t destination_connection_id_length =
        (connection_id_lengths_byte & kDestinationConnectionIdLengthMask) >> 4;
    if (destination_connection_id_length != 0) {
      destination_connection_id_length += kConnectionIdLengthAdjustment;
    }
    uint8_t source_connection_id_length =
        connection_id_lengths_byte & kSourceConnectionIdLengthMask;
    if (source_connection_id_length != 0) {
      source_connection_id_length += kConnectionIdLengthAdjustment;
    }

    // Read destination connection ID.
    if (!reader.ReadConnectionId(&destination_connection_id,
                                 destination_connection_id_length)) {
      detailed_error = "Unable to read destination connection ID.";
      return false;
    }

    // Read source connection ID.
    if (!reader.ReadConnectionId(&source_connection_id,
                                 source_connection_id_length)) {
      detailed_error = "Unable to read source connection ID.";
      return false;
    }
  }
  return true;
}

}  // namespace

// static
QuicErrorCode QuicFramer::ParsePublicHeader(
    QuicDataReader* reader, uint8_t expected_destination_connection_id_length,
    bool ietf_format, uint8_t* first_byte, PacketHeaderFormat* format,
    bool* version_present, bool* has_length_prefix,
    QuicVersionLabel* version_label, ParsedQuicVersion* parsed_version,
    QuicConnectionId* destination_connection_id,
    QuicConnectionId* source_connection_id,
    QuicLongHeaderType* long_packet_type,
    quiche::QuicheVariableLengthIntegerLength* retry_token_length_length,
    absl::string_view* retry_token, std::string* detailed_error) {
  *version_present = false;
  *has_length_prefix = false;
  *version_label = 0;
  *parsed_version = UnsupportedQuicVersion();
  *source_connection_id = EmptyQuicConnectionId();
  *long_packet_type = INVALID_PACKET_TYPE;
  *retry_token_length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
  *retry_token = absl::string_view();
  *detailed_error = "";

  if (!reader->ReadUInt8(first_byte)) {
    *detailed_error = "Unable to read first byte.";
    return QUIC_INVALID_PACKET_HEADER;
  }

  if (!ietf_format) {
    return ParsePublicHeaderGoogleQuic(
        reader, first_byte, format, version_present, version_label,
        parsed_version, destination_connection_id, detailed_error);
  }

  *format = GetIetfPacketHeaderFormat(*first_byte);

  if (*format == IETF_QUIC_SHORT_HEADER_PACKET) {
    if (!reader->ReadConnectionId(destination_connection_id,
                                  expected_destination_connection_id_length)) {
      *detailed_error = "Unable to read destination connection ID.";
      return QUIC_INVALID_PACKET_HEADER;
    }
    return QUIC_NO_ERROR;
  }

  QUICHE_DCHECK_EQ(IETF_QUIC_LONG_HEADER_PACKET, *format);
  *version_present = true;
  if (!ProcessVersionLabel(reader, version_label)) {
    *detailed_error = "Unable to read protocol version.";
    return QUIC_INVALID_PACKET_HEADER;
  }

  if (*version_label == 0) {
    *long_packet_type = VERSION_NEGOTIATION;
  }

  // Parse version.
  *parsed_version = ParseQuicVersionLabel(*version_label);

  // Figure out which IETF QUIC invariants this packet follows.
  *has_length_prefix = PacketHasLengthPrefixedConnectionIds(
      *reader, *parsed_version, *version_label, *first_byte);

  // Parse connection IDs.
  if (!ParseLongHeaderConnectionIds(*reader, *has_length_prefix, *version_label,
                                    *destination_connection_id,
                                    *source_connection_id, *detailed_error)) {
    return QUIC_INVALID_PACKET_HEADER;
  }

  if (!parsed_version->IsKnown()) {
    // Skip parsing of long packet type and retry token for unknown versions.
    return QUIC_NO_ERROR;
  }

  // Parse long packet type.
  *long_packet_type = GetLongHeaderType(*first_byte, *parsed_version);

  switch (*long_packet_type) {
    case INVALID_PACKET_TYPE:
      *detailed_error = "Unable to parse long packet type.";
      return QUIC_INVALID_PACKET_HEADER;
    case INITIAL:
      if (!parsed_version->SupportsRetry()) {
        // Retry token is only present on initial packets for some versions.
        return QUIC_NO_ERROR;
      }
      break;
    default:
      return QUIC_NO_ERROR;
  }

  *retry_token_length_length = reader->PeekVarInt62Length();
  uint64_t retry_token_length;
  if (!reader->ReadVarInt62(&retry_token_length)) {
    *retry_token_length_length = quiche::VARIABLE_LENGTH_INTEGER_LENGTH_0;
    *detailed_error = "Unable to read retry token length.";
    return QUIC_INVALID_PACKET_HEADER;
  }

  if (!reader->ReadStringPiece(retry_token, retry_token_length)) {
    *detailed_error = "Unable to read retry token.";
    return QUIC_INVALID_PACKET_HEADER;
  }

  return QUIC_NO_ERROR;
}

// static
bool QuicFramer::WriteClientVersionNegotiationProbePacket(
    char* packet_bytes, QuicByteCount packet_length,
    const char* destination_connection_id_bytes,
    uint8_t destination_connection_id_length) {
  if (packet_bytes == nullptr) {
    QUIC_BUG(quic_bug_10850_93) << "Invalid packet_bytes";
    return false;
  }
  if (packet_length < kMinPacketSizeForVersionNegotiation ||
      packet_length > 65535) {
    QUIC_BUG(quic_bug_10850_94) << "Invalid packet_length";
    return false;
  }
  if (destination_connection_id_length > kQuicMaxConnectionId4BitLength ||
      destination_connection_id_length < kQuicDefaultConnectionIdLength) {
    QUIC_BUG(quic_bug_10850_95) << "Invalid connection_id_length";
    return false;
  }
  // clang-format off
  const unsigned char packet_start_bytes[] = {
    // IETF long header with fixed bit set, type initial, all-0 encrypted bits.
    0xc0,
    // Version, part of the IETF space reserved for negotiation.
    // This intentionally differs from QuicVersionReservedForNegotiation()
    // to allow differentiating them over the wire.
    0xca, 0xba, 0xda, 0xda,
  };
  // clang-format on
  static_assert(sizeof(packet_start_bytes) == 5, "bad packet_start_bytes size");
  QuicDataWriter writer(packet_length, packet_bytes);
  if (!writer.WriteBytes(packet_start_bytes, sizeof(packet_start_bytes))) {
    QUIC_BUG(quic_bug_10850_96) << "Failed to write packet start";
    return false;
  }

  QuicConnectionId destination_connection_id(destination_connection_id_bytes,
                                             destination_connection_id_length);
  if (!AppendIetfConnectionIds(
          /*version_flag=*/true, /*use_length_prefix=*/true,
          destination_connection_id, EmptyQuicConnectionId(), &writer)) {
    QUIC_BUG(quic_bug_10850_97) << "Failed to write connection IDs";
    return false;
  }
  // Add 8 bytes of zeroes followed by 8 bytes of ones to ensure that this does
  // not parse with any known version. The zeroes make sure that packet numbers,
  // retry token lengths and payload lengths are parsed as zero, and if the
  // zeroes are treated as padding frames, 0xff is known to not parse as a
  // valid frame type.
  if (!writer.WriteUInt64(0) ||
      !writer.WriteUInt64(std::numeric_limits<uint64_t>::max())) {
    QUIC_BUG(quic_bug_10850_98) << "Failed to write 18 bytes";
    return false;
  }
  // Make sure the polite greeting below is padded to a 16-byte boundary to
  // make it easier to read in tcpdump.
  while (writer.length() % 16 != 0) {
    if (!writer.WriteUInt8(0)) {
      QUIC_BUG(quic_bug_10850_99) << "Failed to write padding byte";
      return false;
    }
  }
  // Add a polite greeting in case a human sees this in tcpdump.
  static const char polite_greeting[] =
      "This packet only exists to trigger IETF QUIC version negotiation. "
      "Please respond with a Version Negotiation packet indicating what "
      "versions you support. Thank you and have a nice day.";
  if (!writer.WriteBytes(polite_greeting, sizeof(polite_greeting))) {
    QUIC_BUG(quic_bug_10850_100) << "Failed to write polite greeting";
    return false;
  }
  // Fill the rest of the packet with zeroes.
  writer.WritePadding();
  QUICHE_DCHECK_EQ(0u, writer.remaining());
  return true;
}

// static
bool QuicFramer::ParseServerVersionNegotiationProbeResponse(
    const char* packet_bytes, QuicByteCount packet_length,
    char* source_connection_id_bytes, uint8_t* source_connection_id_length_out,
    std::string* detailed_error) {
  if (detailed_error == nullptr) {
    QUIC_BUG(quic_bug_10850_101) << "Invalid error_details";
    return false;
  }
  *detailed_error = "";
  if (packet_bytes == nullptr) {
    *detailed_error = "Invalid packet_bytes";
    return false;
  }
  if (packet_length < 6) {
    *detailed_error = "Invalid packet_length";
    return false;
  }
  if (source_connection_id_bytes == nullptr) {
    *detailed_error = "Invalid source_connection_id_bytes";
    return false;
  }
  if (source_connection_id_length_out == nullptr) {
    *detailed_error = "Invalid source_connection_id_length_out";
    return false;
  }
  QuicDataReader reader(packet_bytes, packet_length);
  uint8_t type_byte = 0;
  if (!reader.ReadUInt8(&type_byte)) {
    *detailed_error = "Failed to read type byte";
    return false;
  }
  if ((type_byte & 0x80) == 0) {
    *detailed_error = "Packet does not have long header";
    return false;
  }
  uint32_t version = 0;
  if (!reader.ReadUInt32(&version)) {
    *detailed_error = "Failed to read version";
    return false;
  }
  if (version != 0) {
    *detailed_error = "Packet is not a version negotiation packet";
    return false;
  }

  QuicConnectionId destination_connection_id, source_connection_id;
  if (!reader.ReadLengthPrefixedConnectionId(&destination_connection_id)) {
    *detailed_error = "Failed to read destination connection ID";
    return false;
  }
  if (!reader.ReadLengthPrefixedConnectionId(&source_connection_id)) {
    *detailed_error = "Failed to read source connection ID";
    return false;
  }

  if (destination_connection_id.length() != 0) {
    *detailed_error = "Received unexpected destination connection ID length";
    return false;
  }
  if (*source_connection_id_length_out < source_connection_id.length()) {
    *detailed_error =
        absl::StrCat("*source_connection_id_length_out too small ",
                     static_cast<int>(*source_connection_id_length_out), " < ",
                     static_cast<int>(source_connection_id.length()));
    return false;
  }

  memcpy(source_connection_id_bytes, source_connection_id.data(),
         source_connection_id.length());
  *source_connection_id_length_out = source_connection_id.length();

  return true;
}

// Look for and parse the error code from the "<quic_error_code>:" text that
// may be present at the start of the CONNECTION_CLOSE error details string.
// This text, inserted by the peer if it's using Google's QUIC implementation,
// contains additional error information that narrows down the exact error.  If
// the string is not found, or is not properly formed, it returns
// ErrorCode::QUIC_IETF_GQUIC_ERROR_MISSING
void MaybeExtractQuicErrorCode(QuicConnectionCloseFrame* frame) {
  std::vector<absl::string_view> ed = absl::StrSplit(frame->error_details, ':');
  uint64_t extracted_error_code;
  if (ed.size() < 2 || !quiche::QuicheTextUtils::IsAllDigits(ed[0]) ||
      !absl::SimpleAtoi(ed[0], &extracted_error_code) ||
      extracted_error_code >
          std::numeric_limits<
              std::underlying_type<QuicErrorCode>::type>::max()) {
    if (frame->close_type == IETF_QUIC_TRANSPORT_CONNECTION_CLOSE &&
        frame->wire_error_code == NO_IETF_QUIC_ERROR) {
      frame->quic_error_code = QUIC_NO_ERROR;
    } else {
      frame->quic_error_code = QUIC_IETF_GQUIC_ERROR_MISSING;
    }
    return;
  }
  // Return the error code (numeric) and the error details string without the
  // error code prefix. Note that Split returns everything up to, but not
  // including, the split character, so the length of ed[0] is just the number
  // of digits in the error number. In removing the prefix, 1 is added to the
  // length to account for the :
  absl::string_view x = absl::string_view(frame->error_details);
  x.remove_prefix(ed[0].length() + 1);
  frame->error_details = std::string(x);
  frame->quic_error_code = static_cast<QuicErrorCode>(extracted_error_code);
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
