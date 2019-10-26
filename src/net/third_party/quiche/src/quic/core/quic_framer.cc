// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_framer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_handshake_message.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_data_reader.h"
#include "net/third_party/quiche/src/quic/core/quic_data_writer.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_socket_address_coder.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_frame_data_producer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_aligned.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_client_stats.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_fallthrough.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_stack_trace.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

namespace {

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

// Number of bits the packet number length bits are shifted from the right
// edge of the header.
const uint8_t kPublicHeaderSequenceNumberShift = 4;

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
const uint8_t kQuicFrameTypeBrokenMask = 0xE0;   // 0b 11100000
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

uint64_t PacketNumberIntervalLength(
    const QuicInterval<QuicPacketNumber>& interval) {
  if (interval.Empty()) {
    return 0u;
  }
  return interval.max() - interval.min();
}

QuicPacketNumberLength ReadSequenceNumberLength(uint8_t flags) {
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
      QUIC_BUG << "Unreachable case statement.";
      return PACKET_6BYTE_PACKET_NUMBER;
  }
}

QuicPacketNumberLength ReadAckPacketNumberLength(
    QuicTransportVersion /*version*/,
    uint8_t flags) {
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
      QUIC_BUG << "Unreachable case statement.";
      return PACKET_6BYTE_PACKET_NUMBER;
  }
}

uint8_t PacketNumberLengthToOnWireValue(
    QuicPacketNumberLength packet_number_length) {
  return packet_number_length - 1;
}

QuicPacketNumberLength GetShortHeaderPacketNumberLength(uint8_t type) {
  DCHECK(!(type & FLAGS_LONG_HEADER));
  return static_cast<QuicPacketNumberLength>((type & 0x03) + 1);
}

uint8_t LongHeaderTypeToOnWireValue(QuicLongHeaderType type) {
  switch (type) {
    case INITIAL:
      return 0;
    case ZERO_RTT_PROTECTED:
      return 1 << 4;
    case HANDSHAKE:
      return 2 << 4;
    case RETRY:
      return 3 << 4;
    case VERSION_NEGOTIATION:
      return 0xF0;  // Value does not matter
    default:
      QUIC_BUG << "Invalid long header type: " << type;
      return 0xFF;
  }
}

bool GetLongHeaderType(uint8_t type, QuicLongHeaderType* long_header_type) {
  DCHECK((type & FLAGS_LONG_HEADER));
  switch ((type & 0x30) >> 4) {
    case 0:
      *long_header_type = INITIAL;
      break;
    case 1:
      *long_header_type = ZERO_RTT_PROTECTED;
      break;
    case 2:
      *long_header_type = HANDSHAKE;
      break;
    case 3:
      *long_header_type = RETRY;
      break;
    default:
      QUIC_BUG << "Unreachable statement";
      *long_header_type = INVALID_PACKET_TYPE;
      return false;
  }
  return true;
}

QuicPacketNumberLength GetLongHeaderPacketNumberLength(uint8_t type) {
  return static_cast<QuicPacketNumberLength>((type & 0x03) + 1);
}

// Used to get packet number space before packet gets decrypted.
PacketNumberSpace GetPacketNumberSpace(const QuicPacketHeader& header) {
  switch (header.form) {
    case GOOGLE_QUIC_PACKET:
      QUIC_BUG << "Try to get packet number space of Google QUIC packet";
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
          QUIC_BUG << "Try to get packet number space of long header type: "
                   << QuicUtils::QuicLongHeaderTypetoString(
                          header.long_packet_type);
          break;
      }
  }

  return NUM_PACKET_NUMBER_SPACES;
}

EncryptionLevel GetEncryptionLevel(const QuicPacketHeader& header) {
  switch (header.form) {
    case GOOGLE_QUIC_PACKET:
      QUIC_BUG << "Cannot determine EncryptionLevel from Google QUIC header";
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
          QUIC_BUG << "No encryption used with type "
                   << QuicUtils::QuicLongHeaderTypetoString(
                          header.long_packet_type);
      }
  }
  return NUM_ENCRYPTION_LEVELS;
}

QuicStringPiece TruncateErrorString(QuicStringPiece error) {
  if (error.length() <= kMaxErrorStringLength) {
    return error;
  }
  return QuicStringPiece(error.data(), kMaxErrorStringLength);
}

size_t TruncatedErrorStringSize(const QuicStringPiece& error) {
  if (error.length() < kMaxErrorStringLength) {
    return error.length();
  }
  return kMaxErrorStringLength;
}

uint8_t GetConnectionIdLengthValue(QuicConnectionIdLength length) {
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
                             QuicTransportVersion version) {
  return full_packet_number > 0 || version == QUIC_VERSION_99;
}

bool AppendIetfConnectionIds(bool version_flag,
                             bool use_length_prefix,
                             QuicConnectionId destination_connection_id,
                             QuicConnectionId source_connection_id,
                             QuicDataWriter* writer) {
  if (!version_flag) {
    return writer->WriteConnectionId(destination_connection_id);
  }

  if (use_length_prefix) {
    return writer->WriteLengthPrefixedConnectionId(destination_connection_id) &&
           writer->WriteLengthPrefixedConnectionId(source_connection_id);
  }

  // Compute connection ID length byte.
  uint8_t dcil = GetConnectionIdLengthValue(
      static_cast<QuicConnectionIdLength>(destination_connection_id.length()));
  uint8_t scil = GetConnectionIdLengthValue(
      static_cast<QuicConnectionIdLength>(source_connection_id.length()));
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
  return QuicStrCat(std::to_string(static_cast<unsigned>(quic_error_code)), ":",
                    initial_error_string);
}

}  // namespace

QuicFramer::QuicFramer(const ParsedQuicVersionVector& supported_versions,
                       QuicTime creation_time,
                       Perspective perspective,
                       uint8_t expected_server_connection_id_length)
    : visitor_(nullptr),
      error_(QUIC_NO_ERROR),
      last_serialized_server_connection_id_(EmptyQuicConnectionId()),
      last_serialized_client_connection_id_(EmptyQuicConnectionId()),
      version_(PROTOCOL_UNSUPPORTED, QUIC_VERSION_UNSUPPORTED),
      supported_versions_(supported_versions),
      decrypter_level_(ENCRYPTION_INITIAL),
      alternative_decrypter_level_(NUM_ENCRYPTION_LEVELS),
      alternative_decrypter_latch_(false),
      perspective_(perspective),
      validate_flags_(true),
      process_timestamps_(false),
      creation_time_(creation_time),
      last_timestamp_(QuicTime::Delta::Zero()),
      first_sending_packet_number_(FirstSendingPacketNumber()),
      data_producer_(nullptr),
      infer_packet_header_type_from_version_(perspective ==
                                             Perspective::IS_CLIENT),
      expected_server_connection_id_length_(
          expected_server_connection_id_length),
      expected_client_connection_id_length_(0),
      supports_multiple_packet_number_spaces_(false),
      last_written_packet_number_length_(0),
      peer_ack_delay_exponent_(kDefaultAckDelayExponent),
      local_ack_delay_exponent_(kDefaultAckDelayExponent),
      current_received_frame_type_(0) {
  DCHECK(!supported_versions.empty());
  version_ = supported_versions_[0];
  decrypter_[ENCRYPTION_INITIAL] = QuicMakeUnique<NullDecrypter>(perspective);
  encrypter_[ENCRYPTION_INITIAL] = QuicMakeUnique<NullEncrypter>(perspective);
}

QuicFramer::~QuicFramer() {}

// static
size_t QuicFramer::GetMinStreamFrameSize(QuicTransportVersion version,
                                         QuicStreamId stream_id,
                                         QuicStreamOffset offset,
                                         bool last_frame_in_packet,
                                         QuicPacketLength data_length) {
  if (VersionHasIetfQuicFrames(version)) {
    return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(stream_id) +
           (last_frame_in_packet
                ? 0
                : QuicDataWriter::GetVarInt62Len(data_length)) +
           (offset != 0 ? QuicDataWriter::GetVarInt62Len(offset) : 0);
  }
  return kQuicFrameTypeSize + GetStreamIdSize(stream_id) +
         GetStreamOffsetSize(version, offset) +
         (last_frame_in_packet ? 0 : kQuicStreamPayloadLengthSize);
}

// static
size_t QuicFramer::GetMinCryptoFrameSize(QuicStreamOffset offset,
                                         QuicPacketLength data_length) {
  return kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(offset) +
         QuicDataWriter::GetVarInt62Len(data_length);
}

// static
size_t QuicFramer::GetMessageFrameSize(QuicTransportVersion version,
                                       bool last_frame_in_packet,
                                       QuicByteCount length) {
  QUIC_BUG_IF(!VersionSupportsMessageFrames(version))
      << "Try to serialize MESSAGE frame in " << version;
  return kQuicFrameTypeSize +
         (last_frame_in_packet ? 0 : QuicDataWriter::GetVarInt62Len(length)) +
         length;
}

// static
size_t QuicFramer::GetMinAckFrameSize(
    QuicTransportVersion version,
    QuicPacketNumberLength largest_observed_length) {
  if (VersionHasIetfQuicFrames(version)) {
    // The minimal ack frame consists of the following four fields: Largest
    // Acknowledged, ACK Delay, ACK Block Count, and First ACK Block. Minimum
    // size of each is 1 byte.
    return kQuicFrameTypeSize + 4;
  }
  size_t min_size = kQuicFrameTypeSize + largest_observed_length +
                    kQuicDeltaTimeLargestObservedSize;
  return min_size + kQuicNumTimestampsSize;
}

// static
size_t QuicFramer::GetStopWaitingFrameSize(
    QuicTransportVersion /*version*/,
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
    QuicTransportVersion version,
    const QuicConnectionCloseFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    // Not IETF QUIC, return Google QUIC CONNECTION CLOSE frame size.
    return kQuicFrameTypeSize + kQuicErrorCodeSize +
           kQuicErrorDetailsLengthSize +
           TruncatedErrorStringSize(frame.error_details);
  }

  // Prepend the extra error information to the string and get the result's
  // length.
  const size_t truncated_error_string_size = TruncatedErrorStringSize(
      GenerateErrorString(frame.error_details, frame.extracted_error_code));

  uint64_t close_code = 0;
  if (frame.close_type == IETF_QUIC_TRANSPORT_CONNECTION_CLOSE) {
    close_code = static_cast<uint64_t>(frame.transport_error_code);
  } else if (frame.close_type == IETF_QUIC_APPLICATION_CONNECTION_CLOSE) {
    close_code = static_cast<uint64_t>(frame.application_error_code);
  }

  const size_t frame_size =
      truncated_error_string_size +
      QuicDataWriter::GetVarInt62Len(truncated_error_string_size) +
      kQuicFrameTypeSize + QuicDataWriter::GetVarInt62Len(close_code);
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
    QuicTransportVersion version,
    const QuicWindowUpdateFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    return kQuicFrameTypeSize + kQuicMaxStreamIdSize + kQuicMaxStreamOffsetSize;
  }
  if (frame.stream_id == QuicUtils::GetInvalidStreamId(version)) {
    // Frame would be a MAX DATA frame, which has only a Maximum Data field.
    return kQuicFrameTypeSize +
           QuicDataWriter::GetVarInt62Len(frame.byte_offset);
  }
  // Frame would be MAX STREAM DATA, has Maximum Stream Data and Stream ID
  // fields.
  return kQuicFrameTypeSize +
         QuicDataWriter::GetVarInt62Len(frame.byte_offset) +
         QuicDataWriter::GetVarInt62Len(frame.stream_id);
}

// static
size_t QuicFramer::GetMaxStreamsFrameSize(QuicTransportVersion version,
                                          const QuicMaxStreamsFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    QUIC_BUG << "In version " << version
             << ", which does not support IETF Frames, and tried to serialize "
                "MaxStreams Frame.";
  }
  return kQuicFrameTypeSize +
         QuicDataWriter::GetVarInt62Len(frame.stream_count);
}

// static
size_t QuicFramer::GetStreamsBlockedFrameSize(
    QuicTransportVersion version,
    const QuicStreamsBlockedFrame& frame) {
  if (!VersionHasIetfQuicFrames(version)) {
    QUIC_BUG << "In version " << version
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
         QuicDataWriter::GetVarInt62Len(frame.application_error_code);
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
    QuicTransportVersion version,
    const QuicFrame& frame) {
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
      return GetWindowUpdateFrameSize(version, *frame.window_update_frame);
    case BLOCKED_FRAME:
      return GetBlockedFrameSize(version, *frame.blocked_frame);
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
      return GetPathResponseFrameSize(*frame.path_response_frame);
    case PATH_CHALLENGE_FRAME:
      return GetPathChallengeFrameSize(*frame.path_challenge_frame);
    case STOP_SENDING_FRAME:
      return GetStopSendingFrameSize(*frame.stop_sending_frame);

    case STREAM_FRAME:
    case ACK_FRAME:
    case STOP_WAITING_FRAME:
    case MTU_DISCOVERY_FRAME:
    case PADDING_FRAME:
    case MESSAGE_FRAME:
    case CRYPTO_FRAME:
    case NUM_FRAME_TYPES:
      DCHECK(false);
      return 0;
  }

  // Not reachable, but some Chrome compilers can't figure that out.  *sigh*
  DCHECK(false);
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
  QUIC_BUG << "Failed to determine StreamIDSize.";
  return 4;
}

// static
size_t QuicFramer::GetStreamOffsetSize(QuicTransportVersion /*version*/,
                                       QuicStreamOffset offset) {
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
  QUIC_BUG << "Failed to determine StreamOffsetSize.";
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

// TODO(nharper): Change this method to take a ParsedQuicVersion.
bool QuicFramer::IsSupportedTransportVersion(
    const QuicTransportVersion version) const {
  for (ParsedQuicVersion supported_version : supported_versions_) {
    if (version == supported_version.transport_version) {
      return true;
    }
  }
  return false;
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
    const QuicFrame& frame,
    size_t free_bytes,
    bool first_frame,
    bool last_frame,
    QuicPacketNumberLength packet_number_length) {
  // Prevent a rare crash reported in b/19458523.
  if (frame.type == ACK_FRAME && frame.ack_frame == nullptr) {
    QUIC_BUG << "Cannot compute the length of a null ack frame. free_bytes:"
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
      free_bytes >= GetMinAckFrameSize(version_.transport_version,
                                       PACKET_6BYTE_PACKET_NUMBER);
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
          kQuicDefaultLongHeaderLengthLength) {
    set_detailed_error("Invalid length_field_offset.");
    QUIC_BUG << "Invalid length_field_offset.";
    return false;
  }
  size_t length_to_write = writer->length() - length_field_offset -
                           kQuicDefaultLongHeaderLengthLength;
  // Add length of auth tag.
  length_to_write = GetCiphertextSize(level, length_to_write);

  QuicDataWriter length_writer(writer->length() - length_field_offset,
                               writer->data() + length_field_offset);
  if (!length_writer.WriteVarInt62(length_to_write,
                                   kQuicDefaultLongHeaderLengthLength)) {
    set_detailed_error("Failed to overwrite long header length.");
    QUIC_BUG << "Failed to overwrite long header length.";
    return false;
  }
  return true;
}

size_t QuicFramer::BuildDataPacket(const QuicPacketHeader& header,
                                   const QuicFrames& frames,
                                   char* buffer,
                                   size_t packet_length,
                                   EncryptionLevel level) {
  QuicDataWriter writer(packet_length, buffer);
  size_t length_field_offset = 0;
  if (!AppendPacketHeader(header, &writer, &length_field_offset)) {
    QUIC_BUG << "AppendPacketHeader failed";
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
  // TODO(dschinazi) if we enable long header lengths before v99, we need to
  // add support for fixing up lengths in QuicFramer::BuildDataPacket.
  DCHECK(!QuicVersionHasLongHeaderLengths(transport_version()));

  size_t i = 0;
  for (const QuicFrame& frame : frames) {
    // Determine if we should write stream frame length in header.
    const bool last_frame_in_packet = i == frames.size() - 1;
    if (!AppendTypeByte(frame, last_frame_in_packet, &writer)) {
      QUIC_BUG << "AppendTypeByte failed";
      return 0;
    }

    switch (frame.type) {
      case PADDING_FRAME:
        if (!AppendPaddingFrame(frame.padding_frame, &writer)) {
          QUIC_BUG << "AppendPaddingFrame of "
                   << frame.padding_frame.num_padding_bytes << " failed";
          return 0;
        }
        break;
      case STREAM_FRAME:
        if (!AppendStreamFrame(frame.stream_frame, last_frame_in_packet,
                               &writer)) {
          QUIC_BUG << "AppendStreamFrame failed";
          return 0;
        }
        break;
      case ACK_FRAME:
        if (!AppendAckFrameAndTypeByte(*frame.ack_frame, &writer)) {
          QUIC_BUG << "AppendAckFrameAndTypeByte failed: " << detailed_error_;
          return 0;
        }
        break;
      case STOP_WAITING_FRAME:
        if (!AppendStopWaitingFrame(header, frame.stop_waiting_frame,
                                    &writer)) {
          QUIC_BUG << "AppendStopWaitingFrame failed";
          return 0;
        }
        break;
      case MTU_DISCOVERY_FRAME:
        // MTU discovery frames are serialized as ping frames.
        QUIC_FALLTHROUGH_INTENDED;
      case PING_FRAME:
        // Ping has no payload.
        break;
      case RST_STREAM_FRAME:
        if (!AppendRstStreamFrame(*frame.rst_stream_frame, &writer)) {
          QUIC_BUG << "AppendRstStreamFrame failed";
          return 0;
        }
        break;
      case CONNECTION_CLOSE_FRAME:
        if (!AppendConnectionCloseFrame(*frame.connection_close_frame,
                                        &writer)) {
          QUIC_BUG << "AppendConnectionCloseFrame failed";
          return 0;
        }
        break;
      case GOAWAY_FRAME:
        if (!AppendGoAwayFrame(*frame.goaway_frame, &writer)) {
          QUIC_BUG << "AppendGoAwayFrame failed";
          return 0;
        }
        break;
      case WINDOW_UPDATE_FRAME:
        if (!AppendWindowUpdateFrame(*frame.window_update_frame, &writer)) {
          QUIC_BUG << "AppendWindowUpdateFrame failed";
          return 0;
        }
        break;
      case BLOCKED_FRAME:
        if (!AppendBlockedFrame(*frame.blocked_frame, &writer)) {
          QUIC_BUG << "AppendBlockedFrame failed";
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
          QUIC_BUG << "AppendMessageFrame failed";
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
          QUIC_BUG << "AppendCryptoFrame failed";
          return 0;
        }
        break;
      default:
        RaiseError(QUIC_INVALID_FRAME_DATA);
        QUIC_BUG << "QUIC_INVALID_FRAME_DATA";
        return 0;
    }
    ++i;
  }

  return writer.length();
}

size_t QuicFramer::AppendIetfFrames(const QuicFrames& frames,
                                    QuicDataWriter* writer) {
  size_t i = 0;
  for (const QuicFrame& frame : frames) {
    // Determine if we should write stream frame length in header.
    const bool last_frame_in_packet = i == frames.size() - 1;
    if (!AppendIetfTypeByte(frame, last_frame_in_packet, writer)) {
      QUIC_BUG << "AppendIetfTypeByte failed: " << detailed_error();
      return 0;
    }

    switch (frame.type) {
      case PADDING_FRAME:
        if (!AppendPaddingFrame(frame.padding_frame, writer)) {
          QUIC_BUG << "AppendPaddingFrame of "
                   << frame.padding_frame.num_padding_bytes
                   << " failed: " << detailed_error();
          return 0;
        }
        break;
      case STREAM_FRAME:
        if (!AppendStreamFrame(frame.stream_frame, last_frame_in_packet,
                               writer)) {
          QUIC_BUG << "AppendStreamFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case ACK_FRAME:
        if (!AppendIetfAckFrameAndTypeByte(*frame.ack_frame, writer)) {
          QUIC_BUG << "AppendIetfAckFrameAndTypeByte failed: "
                   << detailed_error();
          return 0;
        }
        break;
      case STOP_WAITING_FRAME:
        set_detailed_error(
            "Attempt to append STOP WAITING frame in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case MTU_DISCOVERY_FRAME:
        // MTU discovery frames are serialized as ping frames.
        QUIC_FALLTHROUGH_INTENDED;
      case PING_FRAME:
        // Ping has no payload.
        break;
      case RST_STREAM_FRAME:
        if (!AppendRstStreamFrame(*frame.rst_stream_frame, writer)) {
          QUIC_BUG << "AppendRstStreamFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case CONNECTION_CLOSE_FRAME:
        if (!AppendIetfConnectionCloseFrame(*frame.connection_close_frame,
                                            writer)) {
          QUIC_BUG << "AppendIetfConnectionCloseFrame failed: "
                   << detailed_error();
          return 0;
        }
        break;
      case GOAWAY_FRAME:
        set_detailed_error("Attempt to append GOAWAY frame in IETF QUIC.");
        return RaiseError(QUIC_INTERNAL_ERROR);
      case WINDOW_UPDATE_FRAME:
        // Depending on whether there is a stream ID or not, will be either a
        // MAX STREAM DATA frame or a MAX DATA frame.
        if (frame.window_update_frame->stream_id ==
            QuicUtils::GetInvalidStreamId(transport_version())) {
          if (!AppendMaxDataFrame(*frame.window_update_frame, writer)) {
            QUIC_BUG << "AppendMaxDataFrame failed: " << detailed_error();
            return 0;
          }
        } else {
          if (!AppendMaxStreamDataFrame(*frame.window_update_frame, writer)) {
            QUIC_BUG << "AppendMaxStreamDataFrame failed: " << detailed_error();
            return 0;
          }
        }
        break;
      case BLOCKED_FRAME:
        if (!AppendBlockedFrame(*frame.blocked_frame, writer)) {
          QUIC_BUG << "AppendBlockedFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case MAX_STREAMS_FRAME:
        if (!AppendMaxStreamsFrame(frame.max_streams_frame, writer)) {
          QUIC_BUG << "AppendMaxStreamsFrame failed" << detailed_error();
          return 0;
        }
        break;
      case STREAMS_BLOCKED_FRAME:
        if (!AppendStreamsBlockedFrame(frame.streams_blocked_frame, writer)) {
          QUIC_BUG << "AppendStreamsBlockedFrame failed" << detailed_error();
          return 0;
        }
        break;
      case NEW_CONNECTION_ID_FRAME:
        if (!AppendNewConnectionIdFrame(*frame.new_connection_id_frame,
                                        writer)) {
          QUIC_BUG << "AppendNewConnectionIdFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case RETIRE_CONNECTION_ID_FRAME:
        if (!AppendRetireConnectionIdFrame(*frame.retire_connection_id_frame,
                                           writer)) {
          QUIC_BUG << "AppendRetireConnectionIdFrame failed: "
                   << detailed_error();
          return 0;
        }
        break;
      case NEW_TOKEN_FRAME:
        if (!AppendNewTokenFrame(*frame.new_token_frame, writer)) {
          QUIC_BUG << "AppendNewTokenFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case STOP_SENDING_FRAME:
        if (!AppendStopSendingFrame(*frame.stop_sending_frame, writer)) {
          QUIC_BUG << "AppendStopSendingFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case PATH_CHALLENGE_FRAME:
        if (!AppendPathChallengeFrame(*frame.path_challenge_frame, writer)) {
          QUIC_BUG << "AppendPathChallengeFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case PATH_RESPONSE_FRAME:
        if (!AppendPathResponseFrame(*frame.path_response_frame, writer)) {
          QUIC_BUG << "AppendPathResponseFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case MESSAGE_FRAME:
        if (!AppendMessageFrameAndTypeByte(*frame.message_frame,
                                           last_frame_in_packet, writer)) {
          QUIC_BUG << "AppendMessageFrame failed: " << detailed_error();
          return 0;
        }
        break;
      case CRYPTO_FRAME:
        if (!AppendCryptoFrame(*frame.crypto_frame, writer)) {
          QUIC_BUG << "AppendCryptoFrame failed: " << detailed_error();
          return 0;
        }
        break;
      default:
        RaiseError(QUIC_INVALID_FRAME_DATA);
        set_detailed_error("Tried to append unknown frame type.");
        QUIC_BUG << "QUIC_INVALID_FRAME_DATA";
        return 0;
    }
    ++i;
  }

  return writer->length();
}

size_t QuicFramer::BuildConnectivityProbingPacket(
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

  return BuildDataPacket(header, frames, buffer, packet_length, level);
}

size_t QuicFramer::BuildPaddedPathChallengePacket(
    const QuicPacketHeader& header,
    char* buffer,
    size_t packet_length,
    QuicPathFrameBuffer* payload,
    QuicRandom* randomizer,
    EncryptionLevel level) {
  if (!VersionHasIetfQuicFrames(version_.transport_version)) {
    QUIC_BUG << "Attempt to build a PATH_CHALLENGE Connectivity Probing "
                "packet and not doing IETF QUIC";
    return 0;
  }
  QuicFrames frames;

  // Write a PATH_CHALLENGE frame, which has a random 8-byte payload
  randomizer->RandBytes(payload->data(), payload->size());

  QuicPathChallengeFrame path_challenge_frame(0, *payload);
  frames.push_back(QuicFrame(&path_challenge_frame));

  // Add padding to the rest of the packet in order to assess Path MTU
  // characteristics.
  QuicPaddingFrame padding_frame;
  frames.push_back(QuicFrame(padding_frame));

  return BuildDataPacket(header, frames, buffer, packet_length, level);
}

size_t QuicFramer::BuildPathResponsePacket(
    const QuicPacketHeader& header,
    char* buffer,
    size_t packet_length,
    const QuicDeque<QuicPathFrameBuffer>& payloads,
    const bool is_padded,
    EncryptionLevel level) {
  if (payloads.empty()) {
    QUIC_BUG
        << "Attempt to generate connectivity response with no request payloads";
    return 0;
  }
  if (!VersionHasIetfQuicFrames(version_.transport_version)) {
    QUIC_BUG << "Attempt to build a PATH_RESPONSE Connectivity Probing "
                "packet and not doing IETF QUIC";
    return 0;
  }

  std::vector<std::unique_ptr<QuicPathResponseFrame>> path_response_frames;
  for (const QuicPathFrameBuffer& payload : payloads) {
    // Note that the control frame ID can be 0 since this is not retransmitted.
    path_response_frames.push_back(
        QuicMakeUnique<QuicPathResponseFrame>(0, payload));
  }

  QuicFrames frames;
  for (const std::unique_ptr<QuicPathResponseFrame>& path_response_frame :
       path_response_frames) {
    frames.push_back(QuicFrame(path_response_frame.get()));
  }

  if (is_padded) {
    // Add padding to the rest of the packet in order to assess Path MTU
    // characteristics.
    QuicPaddingFrame padding_frame;
    frames.push_back(QuicFrame(padding_frame));
  }

  return BuildDataPacket(header, frames, buffer, packet_length, level);
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

  return QuicMakeUnique<QuicEncryptedPacket>(buffer.release(), len, true);
}

// static
std::unique_ptr<QuicEncryptedPacket> QuicFramer::BuildIetfStatelessResetPacket(
    QuicConnectionId /*connection_id*/,
    QuicUint128 stateless_reset_token) {
  QUIC_DVLOG(1) << "Building IETF stateless reset packet.";
  size_t len = kPacketHeaderTypeSize + kMinRandomBytesLengthInStatelessReset +
               sizeof(stateless_reset_token);
  std::unique_ptr<char[]> buffer(new char[len]);
  QuicDataWriter writer(len, buffer.get());

  uint8_t type = 0;
  type |= FLAGS_FIXED_BIT;
  type |= FLAGS_SHORT_HEADER_RESERVED_1;
  type |= FLAGS_SHORT_HEADER_RESERVED_2;
  type |= PacketNumberLengthToOnWireValue(PACKET_1BYTE_PACKET_NUMBER);

  // Append type byte.
  if (!writer.WriteUInt8(type)) {
    return nullptr;
  }
  // Append random bytes.
  if (!writer.WriteRandomBytes(QuicRandom::GetInstance(),
                               kMinRandomBytesLengthInStatelessReset)) {
    return nullptr;
  }

  // Append stateless reset token.
  if (!writer.WriteBytes(&stateless_reset_token,
                         sizeof(stateless_reset_token))) {
    return nullptr;
  }
  return QuicMakeUnique<QuicEncryptedPacket>(buffer.release(), len, true);
}

// static
std::unique_ptr<QuicEncryptedPacket> QuicFramer::BuildVersionNegotiationPacket(
    QuicConnectionId server_connection_id,
    QuicConnectionId client_connection_id,
    bool ietf_quic,
    bool use_length_prefix,
    const ParsedQuicVersionVector& versions) {
  ParsedQuicVersionVector wire_versions = versions;
  if (!GetQuicReloadableFlag(quic_version_negotiation_grease)) {
    if (wire_versions.empty()) {
      wire_versions = {QuicVersionReservedForNegotiation()};
    }
  } else {
    // Add a version reserved for negotiation as suggested by the
    // "Using Reserved Versions" section of draft-ietf-quic-transport.
    QUIC_RELOADABLE_FLAG_COUNT_N(quic_version_negotiation_grease, 1, 2);
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
          GetQuicFlag(FLAGS_quic_disable_version_negotiation_grease_randomness);
      if (!disable_randomness) {
        version_index = QuicRandom::GetInstance()->RandUint64() %
                        (wire_versions.size() + 1);
      }
      wire_versions.insert(wire_versions.begin() + version_index,
                           QuicVersionReservedForNegotiation());
    }
  }
  if (ietf_quic) {
    return BuildIetfVersionNegotiationPacket(
        use_length_prefix, server_connection_id, client_connection_id,
        wire_versions);
  }

  // The GQUIC encoding does not support encoding client connection IDs.
  DCHECK(client_connection_id.IsEmpty());
  // The GQUIC encoding does not support length-prefixed connection IDs.
  DCHECK(!use_length_prefix);

  DCHECK(!wire_versions.empty());
  size_t len = kPublicFlagsSize + server_connection_id.length() +
               wire_versions.size() * kQuicVersionSize;
  std::unique_ptr<char[]> buffer(new char[len]);
  QuicDataWriter writer(len, buffer.get());

  uint8_t flags = static_cast<uint8_t>(
      PACKET_PUBLIC_FLAGS_VERSION | PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID |
      // TODO(rch): Remove this QUIC_VERSION_32 is retired.
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

  return QuicMakeUnique<QuicEncryptedPacket>(buffer.release(), len, true);
}

// static
std::unique_ptr<QuicEncryptedPacket>
QuicFramer::BuildIetfVersionNegotiationPacket(
    bool use_length_prefix,
    QuicConnectionId server_connection_id,
    QuicConnectionId client_connection_id,
    const ParsedQuicVersionVector& versions) {
  QUIC_DVLOG(1) << "Building IETF version negotiation packet with"
                << (use_length_prefix ? "" : "out")
                << " length prefix, server_connection_id "
                << server_connection_id << " client_connection_id "
                << client_connection_id << " versions "
                << ParsedQuicVersionVectorToString(versions);
  DCHECK(!versions.empty());
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

  return QuicMakeUnique<QuicEncryptedPacket>(buffer.release(), len, true);
}

bool QuicFramer::ProcessPacket(const QuicEncryptedPacket& packet) {
  QuicDataReader reader(packet.data(), packet.length());

  bool packet_has_ietf_packet_header = false;
  if (infer_packet_header_type_from_version_) {
    packet_has_ietf_packet_header =
        VersionHasIetfInvariantHeader(version_.transport_version);
  } else if (!reader.IsDoneReading()) {
    uint8_t type = reader.PeekByte();
    packet_has_ietf_packet_header = QuicUtils::IsIetfPacketHeader(type);
  }
  if (packet_has_ietf_packet_header) {
    QUIC_DVLOG(1) << ENDPOINT << "Processing IETF QUIC packet.";
  }

  visitor_->OnPacket();

  QuicPacketHeader header;
  if (!ProcessPublicHeader(&reader, packet_has_ietf_packet_header, &header)) {
    DCHECK_NE("", detailed_error_);
    QUIC_DVLOG(1) << ENDPOINT << "Unable to process public header. Error: "
                  << detailed_error_;
    DCHECK_NE("", detailed_error_);
    RecordDroppedPacketReason(DroppedPacketReason::INVALID_PUBLIC_HEADER);
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }

  if (!visitor_->OnUnauthenticatedPublicHeader(header)) {
    // The visitor suppresses further processing of the packet.
    return true;
  }

  if (IsVersionNegotiation(header, packet_has_ietf_packet_header)) {
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
      return RaiseError(QUIC_INVALID_VERSION);
    }
  }

  bool rv;
  if (header.long_packet_type == RETRY) {
    rv = ProcessRetryPacket(&reader, header);
  } else if (header.reset_flag) {
    rv = ProcessPublicResetPacket(&reader, header);
  } else if (packet.length() <= kMaxIncomingPacketSize) {
    // The optimized decryption algorithm implementations run faster when
    // operating on aligned memory.
    QUIC_CACHELINE_ALIGNED char buffer[kMaxIncomingPacketSize];
    if (packet_has_ietf_packet_header) {
      rv = ProcessIetfDataPacket(&reader, &header, packet, buffer,
                                 QUIC_ARRAYSIZE(buffer));
    } else {
      rv = ProcessDataPacket(&reader, &header, packet, buffer,
                             QUIC_ARRAYSIZE(buffer));
    }
  } else {
    std::unique_ptr<char[]> large_buffer(new char[packet.length()]);
    if (packet_has_ietf_packet_header) {
      rv = ProcessIetfDataPacket(&reader, &header, packet, large_buffer.get(),
                                 packet.length());
    } else {
      rv = ProcessDataPacket(&reader, &header, packet, large_buffer.get(),
                             packet.length());
    }
    QUIC_BUG_IF(rv) << "QUIC should never successfully process packets larger"
                    << "than kMaxIncomingPacketSize. packet size:"
                    << packet.length();
  }
  return rv;
}

bool QuicFramer::ProcessVersionNegotiationPacket(
    QuicDataReader* reader,
    const QuicPacketHeader& header) {
  DCHECK_EQ(Perspective::IS_CLIENT, perspective_);

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
  DCHECK_EQ(Perspective::IS_CLIENT, perspective_);

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

  QuicStringPiece retry_token = reader->ReadRemainingPayload();
  visitor_->OnRetryPacket(original_destination_connection_id,
                          header.source_connection_id, retry_token);
  return true;
}

// Seeks the current packet to check for a coalesced packet at the end.
// If the IETF length field only spans part of the outer packet,
// then there is a coalesced packet after this one.
void QuicFramer::MaybeProcessCoalescedPacket(
    const QuicDataReader& encrypted_reader,
    uint64_t remaining_bytes_length,
    const QuicPacketHeader& header) {
  if (header.remaining_packet_length >= remaining_bytes_length) {
    // There is no coalesced packet.
    return;
  }

  QuicStringPiece remaining_data = encrypted_reader.PeekRemainingPayload();
  DCHECK_EQ(remaining_data.length(), remaining_bytes_length);

  const char* coalesced_data =
      remaining_data.data() + header.remaining_packet_length;
  uint64_t coalesced_data_length =
      remaining_bytes_length - header.remaining_packet_length;
  QuicDataReader coalesced_reader(coalesced_data, coalesced_data_length);

  QuicPacketHeader coalesced_header;
  if (!ProcessIetfPacketHeader(&coalesced_reader, &coalesced_header)) {
    QUIC_PEER_BUG << ENDPOINT
                  << "Failed to parse received coalesced header of length "
                  << coalesced_data_length << ": "
                  << QuicTextUtils::HexEncode(coalesced_data,
                                              coalesced_data_length)
                  << " previous header was " << header;
    return;
  }

  if (coalesced_header.destination_connection_id !=
          header.destination_connection_id ||
      (coalesced_header.form != IETF_QUIC_SHORT_HEADER_PACKET &&
       coalesced_header.version != header.version)) {
    QUIC_PEER_BUG << ENDPOINT << "Received mismatched coalesced header "
                  << coalesced_header << " previous header was " << header;
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
    QUIC_BUG << "Length TruncateRemaining failed.";
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }
  return true;
}

bool QuicFramer::ProcessIetfDataPacket(QuicDataReader* encrypted_reader,
                                       QuicPacketHeader* header,
                                       const QuicEncryptedPacket& packet,
                                       char* decrypted_buffer,
                                       size_t buffer_length) {
  DCHECK_NE(GOOGLE_QUIC_PACKET, header->form);
  DCHECK(!header->has_possible_stateless_reset_token);
  header->length_length = VARIABLE_LENGTH_INTEGER_LENGTH_0;
  header->remaining_packet_length = 0;
  if (header->form == IETF_QUIC_SHORT_HEADER_PACKET &&
      perspective_ == Perspective::IS_CLIENT) {
    // Peek possible stateless reset token. Will only be used on decryption
    // failure.
    QuicStringPiece remaining = encrypted_reader->PeekRemainingPayload();
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

  QuicStringPiece associated_data;
  std::vector<char> ad_storage;
  if (header->form == IETF_QUIC_SHORT_HEADER_PACKET ||
      header->long_packet_type != VERSION_NEGOTIATION) {
    DCHECK(header->form == IETF_QUIC_SHORT_HEADER_PACKET ||
           header->long_packet_type == INITIAL ||
           header->long_packet_type == HANDSHAKE ||
           header->long_packet_type == ZERO_RTT_PROTECTED);
    // Process packet number.
    QuicPacketNumber base_packet_number;
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
      if (!RemoveHeaderProtection(encrypted_reader, packet, header,
                                  &full_packet_number, &ad_storage)) {
        hp_removal_failed = true;
      }
      associated_data = QuicStringPiece(ad_storage.data(), ad_storage.size());
    } else if (!ProcessAndCalculatePacketNumber(
                   encrypted_reader, header->packet_number_length,
                   base_packet_number, &full_packet_number)) {
      set_detailed_error("Unable to read packet number.");
      RecordDroppedPacketReason(DroppedPacketReason::INVALID_PACKET_NUMBER);
      return RaiseError(QUIC_INVALID_PACKET_HEADER);
    }

    if (hp_removal_failed ||
        !IsValidFullPacketNumber(full_packet_number, transport_version())) {
      if (IsIetfStatelessResetPacket(*header)) {
        // This is a stateless reset packet.
        QuicIetfStatelessResetPacket packet(
            *header, header->possible_stateless_reset_token);
        visitor_->OnAuthenticatedIetfStatelessResetPacket(packet);
        return true;
      }
      if (hp_removal_failed) {
        if (GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
          QUIC_RESTART_FLAG_COUNT_N(quic_framer_uses_undecryptable_upcall, 5,
                                    7);
          const EncryptionLevel decryption_level = GetEncryptionLevel(*header);
          const bool has_decryption_key =
              decrypter_[decryption_level] != nullptr;
          visitor_->OnUndecryptablePacket(
              QuicEncryptedPacket(encrypted_reader->FullPayload()),
              decryption_level, has_decryption_key);
        }
        set_detailed_error("Unable to decrypt header protection.");
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

  QuicStringPiece encrypted = encrypted_reader->ReadRemainingPayload();
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
  if (!DecryptPayload(encrypted, associated_data, *header, decrypted_buffer,
                      buffer_length, &decrypted_length, &decrypted_level)) {
    if (IsIetfStatelessResetPacket(*header)) {
      // This is a stateless reset packet.
      QuicIetfStatelessResetPacket packet(
          *header, header->possible_stateless_reset_token);
      visitor_->OnAuthenticatedIetfStatelessResetPacket(packet);
      return true;
    }
    if (GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
      QUIC_RESTART_FLAG_COUNT_N(quic_framer_uses_undecryptable_upcall, 6, 7);
      const EncryptionLevel decryption_level = GetEncryptionLevel(*header);
      const bool has_decryption_key = version_.KnowsWhichDecrypterToUse() &&
                                      decrypter_[decryption_level] != nullptr;
      visitor_->OnUndecryptablePacket(
          QuicEncryptedPacket(encrypted_reader->FullPayload()),
          decryption_level, has_decryption_key);
    }
    set_detailed_error("Unable to decrypt payload.");
    RecordDroppedPacketReason(DroppedPacketReason::DECRYPTION_FAILURE);
    return RaiseError(QUIC_DECRYPTION_FAILURE);
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

  if (packet.length() > kMaxIncomingPacketSize) {
    set_detailed_error("Packet too large.");
    return RaiseError(QUIC_PACKET_TOO_LARGE);
  }

  // Handle the payload.
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    current_received_frame_type_ = 0;
    if (!ProcessIetfFrameData(&reader, *header)) {
      current_received_frame_type_ = 0;
      DCHECK_NE(QUIC_NO_ERROR, error_);  // ProcessIetfFrameData sets the error.
      DCHECK_NE("", detailed_error_);
      QUIC_DLOG(WARNING) << ENDPOINT << "Unable to process frame data. Error: "
                         << detailed_error_;
      return false;
    }
    current_received_frame_type_ = 0;
  } else {
    if (!ProcessFrameData(&reader, *header)) {
      DCHECK_NE(QUIC_NO_ERROR, error_);  // ProcessFrameData sets the error.
      DCHECK_NE("", detailed_error_);
      QUIC_DLOG(WARNING) << ENDPOINT << "Unable to process frame data. Error: "
                         << detailed_error_;
      return false;
    }
  }

  visitor_->OnPacketComplete();
  return true;
}

bool QuicFramer::ProcessDataPacket(QuicDataReader* encrypted_reader,
                                   QuicPacketHeader* header,
                                   const QuicEncryptedPacket& packet,
                                   char* decrypted_buffer,
                                   size_t buffer_length) {
  if (!ProcessUnauthenticatedHeader(encrypted_reader, header)) {
    DCHECK_NE("", detailed_error_);
    QUIC_DVLOG(1)
        << ENDPOINT
        << "Unable to process packet header. Stopping parsing. Error: "
        << detailed_error_;
    RecordDroppedPacketReason(DroppedPacketReason::INVALID_PACKET_NUMBER);
    return false;
  }

  QuicStringPiece encrypted = encrypted_reader->ReadRemainingPayload();
  QuicStringPiece associated_data = GetAssociatedDataFromEncryptedPacket(
      version_.transport_version, packet,
      GetIncludedDestinationConnectionIdLength(*header),
      GetIncludedSourceConnectionIdLength(*header), header->version_flag,
      header->nonce != nullptr, header->packet_number_length,
      header->retry_token_length_length, header->retry_token.length(),
      header->length_length);

  size_t decrypted_length = 0;
  EncryptionLevel decrypted_level;
  if (!DecryptPayload(encrypted, associated_data, *header, decrypted_buffer,
                      buffer_length, &decrypted_length, &decrypted_level)) {
    if (GetQuicRestartFlag(quic_framer_uses_undecryptable_upcall)) {
      QUIC_RESTART_FLAG_COUNT_N(quic_framer_uses_undecryptable_upcall, 7, 7);
      const EncryptionLevel decryption_level = decrypter_level_;
      // This version uses trial decryption so we always report to our visitor
      // that we are not certain we have the correct decryption key.
      const bool has_decryption_key = false;
      visitor_->OnUndecryptablePacket(
          QuicEncryptedPacket(encrypted_reader->FullPayload()),
          decryption_level, has_decryption_key);
    }
    RecordDroppedPacketReason(DroppedPacketReason::DECRYPTION_FAILURE);
    set_detailed_error("Unable to decrypt payload.");
    return RaiseError(QUIC_DECRYPTION_FAILURE);
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
    // The visitor suppresses further processing of the packet.
    return true;
  }

  if (packet.length() > kMaxIncomingPacketSize) {
    set_detailed_error("Packet too large.");
    return RaiseError(QUIC_PACKET_TOO_LARGE);
  }

  // Handle the payload.
  if (!ProcessFrameData(&reader, *header)) {
    DCHECK_NE(QUIC_NO_ERROR, error_);  // ProcessFrameData sets the error.
    DCHECK_NE("", detailed_error_);
    QUIC_DLOG(WARNING) << ENDPOINT << "Unable to process frame data. Error: "
                       << detailed_error_;
    return false;
  }

  visitor_->OnPacketComplete();
  return true;
}

bool QuicFramer::ProcessPublicResetPacket(QuicDataReader* reader,
                                          const QuicPacketHeader& header) {
  QuicPublicResetPacket packet(
      GetServerConnectionIdAsRecipient(header, perspective_));

  std::unique_ptr<CryptoHandshakeMessage> reset(
      CryptoFramer::ParseMessage(reader->ReadRemainingPayload()));
  if (!reset.get()) {
    set_detailed_error("Unable to read reset message.");
    RecordDroppedPacketReason(DroppedPacketReason::INVALID_PUBLIC_RESET_PACKET);
    return RaiseError(QUIC_INVALID_PUBLIC_RST_PACKET);
  }
  if (reset->tag() != kPRST) {
    set_detailed_error("Incorrect message tag.");
    RecordDroppedPacketReason(DroppedPacketReason::INVALID_PUBLIC_RESET_PACKET);
    return RaiseError(QUIC_INVALID_PUBLIC_RST_PACKET);
  }

  if (reset->GetUint64(kRNON, &packet.nonce_proof) != QUIC_NO_ERROR) {
    set_detailed_error("Unable to read nonce proof.");
    RecordDroppedPacketReason(DroppedPacketReason::INVALID_PUBLIC_RESET_PACKET);
    return RaiseError(QUIC_INVALID_PUBLIC_RST_PACKET);
  }
  // TODO(satyamshekhar): validate nonce to protect against DoS.

  QuicStringPiece address;
  if (reset->GetStringPiece(kCADR, &address)) {
    QuicSocketAddressCoder address_coder;
    if (address_coder.Decode(address.data(), address.length())) {
      packet.client_address =
          QuicSocketAddress(address_coder.ip(), address_coder.port());
    }
  }

  QuicStringPiece endpoint_id;
  if (perspective_ == Perspective::IS_CLIENT &&
      reset->GetStringPiece(kEPID, &endpoint_id)) {
    packet.endpoint_id = std::string(endpoint_id);
    packet.endpoint_id += '\0';
  }

  visitor_->OnPublicResetPacket(packet);
  return true;
}

bool QuicFramer::IsIetfStatelessResetPacket(
    const QuicPacketHeader& header) const {
  QUIC_BUG_IF(header.has_possible_stateless_reset_token &&
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

bool QuicFramer::AppendPacketHeader(const QuicPacketHeader& header,
                                    QuicDataWriter* writer,
                                    size_t* length_field_offset) {
  if (VersionHasIetfInvariantHeader(transport_version())) {
    return AppendIetfPacketHeader(header, writer, length_field_offset);
  }
  QUIC_DVLOG(1) << ENDPOINT << "Appending header: " << header;
  uint8_t public_flags = 0;
  if (header.reset_flag) {
    public_flags |= PACKET_PUBLIC_FLAGS_RST;
  }
  if (header.version_flag) {
    public_flags |= PACKET_PUBLIC_FLAGS_VERSION;
  }

  public_flags |= GetPacketNumberFlags(header.packet_number_length)
                  << kPublicHeaderSequenceNumberShift;

  if (header.nonce != nullptr) {
    DCHECK_EQ(Perspective::IS_SERVER, perspective_);
    public_flags |= PACKET_PUBLIC_FLAGS_NONCE;
  }

  QuicConnectionId server_connection_id =
      GetServerConnectionIdAsSender(header, perspective_);
  QuicConnectionIdIncluded server_connection_id_included =
      GetServerConnectionIdIncludedAsSender(header, perspective_);
  DCHECK_EQ(CONNECTION_ID_ABSENT,
            GetClientConnectionIdIncludedAsSender(header, perspective_))
      << ENDPOINT << ParsedQuicVersionToString(version_)
      << " invalid header: " << header;

  switch (server_connection_id_included) {
    case CONNECTION_ID_ABSENT:
      if (!writer->WriteUInt8(public_flags |
                              PACKET_PUBLIC_FLAGS_0BYTE_CONNECTION_ID)) {
        return false;
      }
      break;
    case CONNECTION_ID_PRESENT:
      QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(
          server_connection_id, transport_version()))
          << "AppendPacketHeader: attempted to use connection ID "
          << server_connection_id << " which is invalid with version "
          << QuicVersionToString(transport_version());

      public_flags |= PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID;
      if (perspective_ == Perspective::IS_CLIENT) {
        public_flags |= PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID_OLD;
      }
      if (!writer->WriteUInt8(public_flags) ||
          !writer->WriteConnectionId(server_connection_id)) {
        return false;
      }
      break;
  }
  last_serialized_server_connection_id_ = server_connection_id;

  if (header.version_flag) {
    DCHECK_EQ(Perspective::IS_CLIENT, perspective_);
    QuicVersionLabel version_label = CreateQuicVersionLabel(version_);
    if (!writer->WriteUInt32(version_label)) {
      return false;
    }

    QUIC_DVLOG(1) << ENDPOINT << "label = '"
                  << QuicVersionLabelToString(version_label) << "'";
  }

  if (header.nonce != nullptr &&
      !writer->WriteBytes(header.nonce, kDiversificationNonceSize)) {
    return false;
  }

  if (!AppendPacketNumber(header.packet_number_length, header.packet_number,
                          writer)) {
    return false;
  }

  return true;
}

bool QuicFramer::AppendIetfHeaderTypeByte(const QuicPacketHeader& header,
                                          QuicDataWriter* writer) {
  uint8_t type = 0;
  if (header.version_flag) {
    type = static_cast<uint8_t>(
        FLAGS_LONG_HEADER | FLAGS_FIXED_BIT |
        LongHeaderTypeToOnWireValue(header.long_packet_type) |
        PacketNumberLengthToOnWireValue(header.packet_number_length));
  } else {
    type = static_cast<uint8_t>(
        FLAGS_FIXED_BIT |
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
  QUIC_BUG_IF(!QuicUtils::IsConnectionIdValidForVersion(server_connection_id,
                                                        transport_version()))
      << "AppendIetfPacketHeader: attempted to use connection ID "
      << server_connection_id << " which is invalid with version "
      << QuicVersionToString(transport_version());
  if (!AppendIetfHeaderTypeByte(header, writer)) {
    return false;
  }

  if (header.version_flag) {
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
  if (version_.SupportsClientConnectionIds()) {
    last_serialized_client_connection_id_ =
        GetClientConnectionIdAsSender(header, perspective_);
  }

  if (QuicVersionHasLongHeaderLengths(transport_version()) &&
      header.version_flag) {
    if (header.long_packet_type == INITIAL) {
      DCHECK_NE(VARIABLE_LENGTH_INTEGER_LENGTH_0,
                header.retry_token_length_length)
          << ENDPOINT << ParsedQuicVersionToString(version_)
          << " bad retry token length length in header: " << header;
      // Write retry token length.
      if (!writer->WriteVarInt62(header.retry_token.length(),
                                 header.retry_token_length_length)) {
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
    DCHECK(header.version_flag);
    DCHECK_EQ(ZERO_RTT_PROTECTED, header.long_packet_type);
    DCHECK_EQ(Perspective::IS_SERVER, perspective_);
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
    QuicPacketNumber base_packet_number,
    uint64_t packet_number) const {
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

bool QuicFramer::ProcessPublicHeader(QuicDataReader* reader,
                                     bool packet_has_ietf_packet_header,
                                     QuicPacketHeader* header) {
  if (packet_has_ietf_packet_header) {
    return ProcessIetfPacketHeader(reader, header);
  }
  uint8_t public_flags;
  if (!reader->ReadBytes(&public_flags, 1)) {
    set_detailed_error("Unable to read public flags.");
    return false;
  }

  header->reset_flag = (public_flags & PACKET_PUBLIC_FLAGS_RST) != 0;
  header->version_flag = (public_flags & PACKET_PUBLIC_FLAGS_VERSION) != 0;

  if (validate_flags_ && !header->version_flag &&
      public_flags > PACKET_PUBLIC_FLAGS_MAX) {
    set_detailed_error("Illegal public flags value.");
    return false;
  }

  if (header->reset_flag && header->version_flag) {
    set_detailed_error("Got version flag in reset packet");
    return false;
  }

  QuicConnectionId* header_connection_id = &header->destination_connection_id;
  QuicConnectionIdIncluded* header_connection_id_included =
      &header->destination_connection_id_included;
  if (perspective_ == Perspective::IS_CLIENT) {
    header_connection_id = &header->source_connection_id;
    header_connection_id_included = &header->source_connection_id_included;
  }
  switch (public_flags & PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID) {
    case PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID:
      if (!reader->ReadConnectionId(header_connection_id,
                                    kQuicDefaultConnectionIdLength)) {
        set_detailed_error("Unable to read ConnectionId.");
        return false;
      }
      *header_connection_id_included = CONNECTION_ID_PRESENT;
      break;
    case PACKET_PUBLIC_FLAGS_0BYTE_CONNECTION_ID:
      *header_connection_id_included = CONNECTION_ID_ABSENT;
      *header_connection_id = last_serialized_server_connection_id_;
      break;
  }

  header->packet_number_length = ReadSequenceNumberLength(
      public_flags >> kPublicHeaderSequenceNumberShift);

  // Read the version only if the packet is from the client.
  // version flag from the server means version negotiation packet.
  if (header->version_flag && perspective_ == Perspective::IS_SERVER) {
    QuicVersionLabel version_label;
    if (!ProcessVersionLabel(reader, &version_label)) {
      set_detailed_error("Unable to read protocol version.");
      return false;
    }
    // If the version from the new packet is the same as the version of this
    // framer, then the public flags should be set to something we understand.
    // If not, this raises an error.
    ParsedQuicVersion version = ParseQuicVersionLabel(version_label);
    if (version == version_ && public_flags > PACKET_PUBLIC_FLAGS_MAX) {
      set_detailed_error("Illegal public flags value.");
      return false;
    }
    header->version = version;
  }

  // A nonce should only be present in packets from the server to the client,
  // which are neither version negotiation nor public reset packets.
  if (public_flags & PACKET_PUBLIC_FLAGS_NONCE &&
      !(public_flags & PACKET_PUBLIC_FLAGS_VERSION) &&
      !(public_flags & PACKET_PUBLIC_FLAGS_RST) &&
      // The nonce flag from a client is ignored and is assumed to be an older
      // client indicating an eight-byte connection ID.
      perspective_ == Perspective::IS_CLIENT) {
    if (!reader->ReadBytes(reinterpret_cast<uint8_t*>(last_nonce_.data()),
                           last_nonce_.size())) {
      set_detailed_error("Unable to read nonce.");
      return false;
    }
    header->nonce = &last_nonce_;
  } else {
    header->nonce = nullptr;
  }

  return true;
}

// static
QuicPacketNumberLength QuicFramer::GetMinPacketNumberLength(
    QuicTransportVersion /*version*/,
    QuicPacketNumber packet_number) {
  DCHECK(packet_number.IsInitialized());
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
      QUIC_BUG << "Unreachable case statement.";
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
  new_ack_info.max_block_length = PacketNumberIntervalLength(*itr);
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
    new_ack_info.max_block_length = std::max(
        new_ack_info.max_block_length, PacketNumberIntervalLength(interval));
  }
  return new_ack_info;
}

bool QuicFramer::ProcessUnauthenticatedHeader(QuicDataReader* encrypted_reader,
                                              QuicPacketHeader* header) {
  QuicPacketNumber base_packet_number;
  if (supports_multiple_packet_number_spaces_) {
    PacketNumberSpace pn_space = GetPacketNumberSpace(*header);
    if (pn_space == NUM_PACKET_NUMBER_SPACES) {
      set_detailed_error("Unable to determine packet number space.");
      return RaiseError(QUIC_INVALID_PACKET_HEADER);
    }
    base_packet_number = largest_decrypted_packet_numbers_[pn_space];
  } else {
    base_packet_number = largest_packet_number_;
  }
  uint64_t full_packet_number;
  if (!ProcessAndCalculatePacketNumber(
          encrypted_reader, header->packet_number_length, base_packet_number,
          &full_packet_number)) {
    set_detailed_error("Unable to read packet number.");
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }

  if (!IsValidFullPacketNumber(full_packet_number, transport_version())) {
    set_detailed_error("packet numbers cannot be 0.");
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }
  header->packet_number = QuicPacketNumber(full_packet_number);

  if (!visitor_->OnUnauthenticatedHeader(*header)) {
    set_detailed_error(
        "Visitor asked to stop processing of unauthenticated header.");
    return false;
  }
  // The function we are in is called because the framer believes that it is
  // processing a packet that uses the non-IETF (i.e. Google QUIC) packet header
  // type. Usually, the framer makes that decision based on the framer's
  // version, but when the framer is used with Perspective::IS_SERVER, then
  // before version negotiation is complete (specifically, before
  // InferPacketHeaderTypeFromVersion is called), this decision is made based on
  // the type byte of the packet.
  //
  // If the framer's version KnowsWhichDecrypterToUse, then that version expects
  // to use the IETF packet header type. If that's the case and we're in this
  // function, then the packet received is invalid: the framer was expecting an
  // IETF packet header and didn't get one.
  if (version().KnowsWhichDecrypterToUse()) {
    set_detailed_error("Invalid public header type for expected version.");
    return RaiseError(QUIC_INVALID_PACKET_HEADER);
  }
  return true;
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
      if (header->version.transport_version != QUIC_VERSION_UNSUPPORTED) {
        if (!(type & FLAGS_FIXED_BIT)) {
          set_detailed_error("Fixed bit is 0 in long header.");
          return false;
        }
        if (!GetLongHeaderType(type, &header->long_packet_type)) {
          set_detailed_error("Illegal long header type value.");
          return false;
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
        } else if (!header->version.HasHeaderProtection()) {
          header->packet_number_length = GetLongHeaderPacketNumberLength(type);
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
  if (!header->version.HasHeaderProtection()) {
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
    QuicDataReader* reader,
    ParsedQuicVersion version,
    Perspective perspective,
    bool should_update_expected_server_connection_id_length,
    uint8_t* expected_server_connection_id_length,
    uint8_t* destination_connection_id_length,
    uint8_t* source_connection_id_length,
    std::string* detailed_error) {
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
      !QuicUtils::VariableLengthConnectionIdAllowedForVersion(
          version.transport_version)) {
    // TODO(dschinazi): use the framer's version once the
    // OnProtocolVersionMismatch call is moved to before this is run.
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
  if (!QuicUtils::IsConnectionIdValidForVersion(
          GetServerConnectionIdAsRecipient(header, perspective_),
          transport_version())) {
    set_detailed_error("Received server connection ID with invalid length.");
    return false;
  }

  if (version_.SupportsClientConnectionIds() &&
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
        reader, expected_destination_connection_id_length,
        VersionHasIetfInvariantHeader(version_.transport_version),
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
    if (header->source_connection_id_included == CONNECTION_ID_ABSENT) {
      DCHECK(header->source_connection_id.IsEmpty());
      if (perspective_ == Perspective::IS_CLIENT) {
        header->source_connection_id = last_serialized_server_connection_id_;
      } else {
        header->source_connection_id = last_serialized_client_connection_id_;
      }
    }

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
    if (!header->version.HasHeaderProtection()) {
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
      DCHECK(!version_.SupportsClientConnectionIds());
      set_detailed_error("Client connection ID not supported in this version.");
      return false;
    }
    if (perspective_ == Perspective::IS_CLIENT) {
      header->source_connection_id = last_serialized_server_connection_id_;
    } else {
      header->source_connection_id = last_serialized_client_connection_id_;
    }
  }

  return ValidateReceivedConnectionIds(*header);
}

bool QuicFramer::ProcessAndCalculatePacketNumber(
    QuicDataReader* reader,
    QuicPacketNumberLength packet_number_length,
    QuicPacketNumber base_packet_number,
    uint64_t* packet_number) {
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
  DCHECK(!VersionHasIetfQuicFrames(version_.transport_version))
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
    const uint8_t special_mask = transport_version() <= QUIC_VERSION_43
                                     ? kQuicFrameTypeBrokenMask
                                     : kQuicFrameTypeSpecialMask;
    if (frame_type & special_mask) {
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
        if (GetQuicReloadableFlag(quic_do_not_accept_stop_waiting) &&
            version_.transport_version > QUIC_VERSION_43) {
          QUIC_RELOADABLE_FLAG_COUNT(quic_do_not_accept_stop_waiting);
          set_detailed_error("STOP WAITING not supported in version 44+.");
          return RaiseError(QUIC_INVALID_STOP_WAITING_DATA);
        }
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
        QUIC_FALLTHROUGH_INTENDED;
      case IETF_EXTENSION_MESSAGE: {
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
        if (!ProcessCryptoFrame(reader, &frame)) {
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

      default:
        set_detailed_error("Illegal frame type.");
        QUIC_DLOG(WARNING) << ENDPOINT << "Illegal frame type: "
                           << static_cast<int>(frame_type);
        return RaiseError(QUIC_INVALID_FRAME_DATA);
    }
  }

  return true;
}

bool QuicFramer::ProcessIetfFrameData(QuicDataReader* reader,
                                      const QuicPacketHeader& header) {
  DCHECK(VersionHasIetfQuicFrames(version_.transport_version))
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
          // TODO(fkastenholz): Or should we create a new visitor function,
          // OnMaxDataFrame()?
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
          // TODO(fkastenholz): Or should we create a new visitor function,
          // OnMaxStreamDataFrame()?
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
        case IETF_BLOCKED: {
          QuicBlockedFrame frame;
          if (!ProcessIetfBlockedFrame(reader, &frame)) {
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
        case IETF_STREAM_BLOCKED: {
          QuicBlockedFrame frame;
          if (!ProcessStreamBlockedFrame(reader, &frame)) {
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
          QUIC_CODE_COUNT_N(quic_streams_blocked_received, 1, 2);
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
        case IETF_EXTENSION_MESSAGE_NO_LENGTH:
          QUIC_FALLTHROUGH_INTENDED;
        case IETF_EXTENSION_MESSAGE: {
          QuicMessageFrame message_frame;
          if (!ProcessMessageFrame(
                  reader, frame_type == IETF_EXTENSION_MESSAGE_NO_LENGTH,
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
          if (!ProcessCryptoFrame(reader, &frame)) {
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
  DCHECK_LE(val, GetMaskFromNumBits(num_bits));
  *flags |= val << offset;
}

// Set the bit at position |offset| to |val| in |flags|.
void SetBit(uint8_t* flags, bool val, uint8_t offset) {
  SetBits(flags, val ? 1 : 0, 1, offset);
}
}  // namespace

bool QuicFramer::ProcessStreamFrame(QuicDataReader* reader,
                                    uint8_t frame_type,
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

  // TODO(ianswett): Don't use QuicStringPiece as an intermediary.
  QuicStringPiece data;
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
  if (!reader->ReadVarIntU32(&frame->stream_id)) {
    set_detailed_error("Unable to read stream_id.");
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
    QuicIetfStreamDataLength length;
    if (!reader->ReadVarInt62(&length)) {
      set_detailed_error("Unable to read stream data length.");
      return false;
    }
    if (length > 0xffff) {
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

  // TODO(ianswett): Don't use QuicStringPiece as an intermediary.
  QuicStringPiece data;
  if (!reader->ReadStringPiece(&data, frame->data_length)) {
    set_detailed_error("Unable to read frame data.");
    return false;
  }
  frame->data_buffer = data.data();
  frame->data_length = static_cast<QuicIetfStreamDataLength>(data.length());

  return true;
}

bool QuicFramer::ProcessCryptoFrame(QuicDataReader* reader,
                                    QuicCryptoFrame* frame) {
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

  // TODO(ianswett): Don't use QuicStringPiece as an intermediary.
  QuicStringPiece data;
  if (!reader->ReadStringPiece(&data, frame->data_length)) {
    set_detailed_error("Unable to read frame data.");
    return false;
  }
  frame->data_buffer = data.data();
  return true;
}

bool QuicFramer::ProcessAckFrame(QuicDataReader* reader, uint8_t frame_type) {
  const bool has_ack_blocks =
      ExtractBit(frame_type, kQuicHasMultipleAckBlocksOffset);
  uint8_t num_ack_blocks = 0;
  uint8_t num_received_packets = 0;

  // Determine the two lengths from the frame type: largest acked length,
  // ack block length.
  const QuicPacketNumberLength ack_block_length = ReadAckPacketNumberLength(
      version_.transport_version,
      ExtractBits(frame_type, kQuicSequenceNumberLengthNumBits,
                  kActBlockLengthOffset));
  const QuicPacketNumberLength largest_acked_length = ReadAckPacketNumberLength(
      version_.transport_version,
      ExtractBits(frame_type, kQuicSequenceNumberLengthNumBits,
                  kLargestAckedOffset));

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
    set_detailed_error(QuicStrCat("Underflow with first ack block length ",
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
        set_detailed_error(
            QuicStrCat("Underflow with ack block length ", current_block_length,
                       ", end of block is ", first_received - gap, ".")
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
  return visitor_->OnAckFrameEnd(QuicPacketNumber(first_received));
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
    set_detailed_error(QuicStrCat("delta_from_largest_observed too high: ",
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
          QuicStrCat("delta_from_largest_observed too high: ",
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

  if (ack_delay_time_in_us == kVarInt62MaxValue) {
    ack_frame->ack_delay_time = QuicTime::Delta::Infinite();
  } else {
    ack_delay_time_in_us = (ack_delay_time_in_us << peer_ack_delay_exponent_);
    ack_frame->ack_delay_time =
        QuicTime::Delta::FromMicroseconds(ack_delay_time_in_us);
  }
  if (frame_type == IETF_ACK_ECN) {
    ack_frame->ecn_counters_populated = true;
    if (!reader->ReadVarInt62(&ack_frame->ect_0_count)) {
      set_detailed_error("Unable to read ack ect_0_count.");
      return false;
    }
    if (!reader->ReadVarInt62(&ack_frame->ect_1_count)) {
      set_detailed_error("Unable to read ack ect_1_count.");
      return false;
    }
    if (!reader->ReadVarInt62(&ack_frame->ecn_ce_count)) {
      set_detailed_error("Unable to read ack ecn_ce_count.");
      return false;
    }
  } else {
    ack_frame->ecn_counters_populated = false;
    ack_frame->ect_0_count = 0;
    ack_frame->ect_1_count = 0;
    ack_frame->ecn_ce_count = 0;
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
    set_detailed_error(QuicStrCat("Underflow with first ack block length ",
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
          QuicStrCat("Underflow with gap block length ", gap_block_value + 1,
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
          QuicStrCat("Underflow with ack block length ", ack_block_value + 1,
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

  return visitor_->OnAckFrameEnd(QuicPacketNumber(block_low));
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

  if (error_code >= QUIC_LAST_ERROR) {
    // Ignore invalid QUIC error code if any.
    error_code = QUIC_LAST_ERROR;
  }

  frame->quic_error_code = static_cast<QuicErrorCode>(error_code);

  // For Google QUIC connection closes, copy the Google QUIC error code to
  // the extracted error code field so that the Google QUIC error code is always
  // available in extracted_error_code.
  frame->extracted_error_code = frame->quic_error_code;

  QuicStringPiece error_details;
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

  if (error_code >= QUIC_LAST_ERROR) {
    // Ignore invalid QUIC error code if any.
    error_code = QUIC_LAST_ERROR;
  }
  frame->error_code = static_cast<QuicErrorCode>(error_code);

  uint32_t stream_id;
  if (!reader->ReadUInt32(&stream_id)) {
    set_detailed_error("Unable to read last good stream id.");
    return false;
  }
  frame->last_good_stream_id = static_cast<QuicStreamId>(stream_id);

  QuicStringPiece reason_phrase;
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

  if (!reader->ReadUInt64(&frame->byte_offset)) {
    set_detailed_error("Unable to read window byte_offset.");
    return false;
  }

  return true;
}

bool QuicFramer::ProcessBlockedFrame(QuicDataReader* reader,
                                     QuicBlockedFrame* frame) {
  DCHECK(!VersionHasIetfQuicFrames(version_.transport_version))
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
    DCHECK_EQ(0x00, next_byte);
    ++frame->num_padding_bytes;
  }
}

bool QuicFramer::ProcessMessageFrame(QuicDataReader* reader,
                                     bool no_message_length,
                                     QuicMessageFrame* frame) {
  if (no_message_length) {
    QuicStringPiece remaining(reader->ReadRemainingPayload());
    frame->data = remaining.data();
    frame->message_length = remaining.length();
    return true;
  }

  uint64_t message_length;
  if (!reader->ReadVarInt62(&message_length)) {
    set_detailed_error("Unable to read message length");
    return false;
  }

  QuicStringPiece message_piece;
  if (!reader->ReadStringPiece(&message_piece, message_length)) {
    set_detailed_error("Unable to read message data");
    return false;
  }

  frame->data = message_piece.data();
  frame->message_length = message_length;

  return true;
}

// static
QuicStringPiece QuicFramer::GetAssociatedDataFromEncryptedPacket(
    QuicTransportVersion version,
    const QuicEncryptedPacket& encrypted,
    QuicConnectionIdLength destination_connection_id_length,
    QuicConnectionIdLength source_connection_id_length,
    bool includes_version,
    bool includes_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    QuicVariableLengthIntegerLength retry_token_length_length,
    uint64_t retry_token_length,
    QuicVariableLengthIntegerLength length_length) {
  // TODO(ianswett): This is identical to QuicData::AssociatedData.
  return QuicStringPiece(
      encrypted.data(),
      GetStartOfEncryptedData(version, destination_connection_id_length,
                              source_connection_id_length, includes_version,
                              includes_diversification_nonce,
                              packet_number_length, retry_token_length_length,
                              retry_token_length, length_length));
}

void QuicFramer::SetDecrypter(EncryptionLevel level,
                              std::unique_ptr<QuicDecrypter> decrypter) {
  DCHECK_EQ(alternative_decrypter_level_, NUM_ENCRYPTION_LEVELS);
  DCHECK_GE(level, decrypter_level_);
  DCHECK(!version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Setting decrypter from level "
                << QuicUtils::EncryptionLevelToString(decrypter_level_)
                << " to " << QuicUtils::EncryptionLevelToString(level);
  decrypter_[decrypter_level_] = nullptr;
  decrypter_[level] = std::move(decrypter);
  decrypter_level_ = level;
}

void QuicFramer::SetAlternativeDecrypter(
    EncryptionLevel level,
    std::unique_ptr<QuicDecrypter> decrypter,
    bool latch_once_used) {
  DCHECK_NE(level, decrypter_level_);
  DCHECK(!version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Setting alternative decrypter from level "
                << QuicUtils::EncryptionLevelToString(
                       alternative_decrypter_level_)
                << " to " << QuicUtils::EncryptionLevelToString(level);
  if (alternative_decrypter_level_ != NUM_ENCRYPTION_LEVELS) {
    decrypter_[alternative_decrypter_level_] = nullptr;
  }
  decrypter_[level] = std::move(decrypter);
  alternative_decrypter_level_ = level;
  alternative_decrypter_latch_ = latch_once_used;
}

void QuicFramer::InstallDecrypter(EncryptionLevel level,
                                  std::unique_ptr<QuicDecrypter> decrypter) {
  DCHECK(version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Installing decrypter at level "
                << QuicUtils::EncryptionLevelToString(level);
  decrypter_[level] = std::move(decrypter);
}

void QuicFramer::RemoveDecrypter(EncryptionLevel level) {
  DCHECK(version_.KnowsWhichDecrypterToUse());
  QUIC_DVLOG(1) << ENDPOINT << "Removing decrypter at level "
                << QuicUtils::EncryptionLevelToString(level);
  decrypter_[level] = nullptr;
}

const QuicDecrypter* QuicFramer::GetDecrypter(EncryptionLevel level) const {
  DCHECK(version_.KnowsWhichDecrypterToUse());
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
  DCHECK_GE(level, 0);
  DCHECK_LT(level, NUM_ENCRYPTION_LEVELS);
  QUIC_DVLOG(1) << ENDPOINT << "Setting encrypter at level "
                << QuicUtils::EncryptionLevelToString(level);
  encrypter_[level] = std::move(encrypter);
}

size_t QuicFramer::EncryptInPlace(EncryptionLevel level,
                                  QuicPacketNumber packet_number,
                                  size_t ad_len,
                                  size_t total_len,
                                  size_t buffer_len,
                                  char* buffer) {
  DCHECK(packet_number.IsInitialized());
  if (encrypter_[level] == nullptr) {
    QUIC_BUG << ENDPOINT
             << "Attempted to encrypt in place without encrypter at level "
             << QuicUtils::EncryptionLevelToString(level);
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }

  size_t output_length = 0;
  if (!encrypter_[level]->EncryptPacket(
          packet_number.ToUint64(),
          QuicStringPiece(buffer, ad_len),  // Associated data
          QuicStringPiece(buffer + ad_len, total_len - ad_len),  // Plaintext
          buffer + ad_len,  // Destination buffer
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

bool QuicFramer::ApplyHeaderProtection(EncryptionLevel level,
                                       char* buffer,
                                       size_t buffer_len,
                                       size_t ad_len) {
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
  QuicStringPiece sample;
  if (!sample_reader.Seek(sample_offset) ||
      !sample_reader.ReadStringPiece(&sample, kHPSampleLen)) {
    QUIC_BUG << "Not enough bytes to sample: sample_offset " << sample_offset
             << ", sample len: " << kHPSampleLen
             << ", buffer len: " << buffer_len;
    return false;
  }

  std::string mask = encrypter_[level]->GenerateHeaderProtectionMask(sample);
  if (mask.empty()) {
    QUIC_BUG << "Unable to generate header protection mask.";
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
    if (!GetLongHeaderType(type_byte, &header_type)) {
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
      QUIC_BUG << "Expected diversification nonce, but not enough bytes";
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
    uint8_t mask_byte;
    if (!mask_reader.ReadUInt8(&mask_byte) ||
        !buffer_reader.ReadUInt8(&buffer_byte) ||
        !buffer_writer.WriteUInt8(buffer_byte ^ mask_byte)) {
      return false;
    }
  }
  return true;
}

bool QuicFramer::RemoveHeaderProtection(QuicDataReader* reader,
                                        const QuicEncryptedPacket& packet,
                                        QuicPacketHeader* header,
                                        uint64_t* full_packet_number,
                                        std::vector<char>* associated_data) {
  EncryptionLevel expected_decryption_level = GetEncryptionLevel(*header);
  QuicDecrypter* decrypter = decrypter_[expected_decryption_level].get();
  if (decrypter == nullptr) {
    QUIC_DVLOG(1)
        << ENDPOINT
        << "No decrypter available for removing header protection at level "
        << QuicUtils::EncryptionLevelToString(expected_decryption_level);
    return false;
  }

  bool has_diversification_nonce =
      header->form == IETF_QUIC_LONG_HEADER_PACKET &&
      header->long_packet_type == ZERO_RTT_PROTECTED &&
      perspective_ == Perspective::IS_CLIENT &&
      version_.handshake_protocol == PROTOCOL_QUIC_CRYPTO;

  // Read a sample from the ciphertext and compute the mask to use for header
  // protection.
  QuicStringPiece remaining_packet = reader->PeekRemainingPayload();
  QuicDataReader sample_reader(remaining_packet);

  // The sample starts 4 bytes after the start of the packet number.
  QuicStringPiece pn;
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
  std::string mask = decrypter->GenerateHeaderProtectionMask(&sample_reader);
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
  QuicDataWriter pn_writer(QUIC_ARRAYSIZE(pn_buffer), pn_buffer);

  // Read the (protected) packet number from the reader and unmask the packet
  // number.
  for (size_t i = 0; i < header->packet_number_length; ++i) {
    uint8_t protected_pn_byte, mask_byte;
    if (!mask_reader.ReadUInt8(&mask_byte) ||
        !reader->ReadUInt8(&protected_pn_byte) ||
        !pn_writer.WriteUInt8(protected_pn_byte ^ mask_byte)) {
      QUIC_DVLOG(1) << "Failed to unmask packet number";
      return false;
    }
  }
  QuicDataReader packet_number_reader(pn_writer.data(), pn_writer.length());
  QuicPacketNumber base_packet_number;
  if (supports_multiple_packet_number_spaces_) {
    PacketNumberSpace pn_space = GetPacketNumberSpace(*header);
    if (pn_space == NUM_PACKET_NUMBER_SPACES) {
      return false;
    }
    base_packet_number = largest_decrypted_packet_numbers_[pn_space];
  } else {
    base_packet_number = largest_packet_number_;
  }
  if (!ProcessAndCalculatePacketNumber(
          &packet_number_reader, header->packet_number_length,
          base_packet_number, full_packet_number)) {
    return false;
  }

  // Get the associated data, and apply the same unmasking operations to it.
  QuicStringPiece ad = GetAssociatedDataFromEncryptedPacket(
      version_.transport_version, packet,
      GetIncludedDestinationConnectionIdLength(*header),
      GetIncludedSourceConnectionIdLength(*header), header->version_flag,
      has_diversification_nonce, header->packet_number_length,
      header->retry_token_length_length, header->retry_token.length(),
      header->length_length);
  *associated_data = std::vector<char>(ad.begin(), ad.end());
  QuicDataWriter ad_writer(associated_data->size(), associated_data->data());

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
                                  const QuicPacket& packet,
                                  char* buffer,
                                  size_t buffer_len) {
  DCHECK(packet_number.IsInitialized());
  if (encrypter_[level] == nullptr) {
    QUIC_BUG << ENDPOINT << "Attempted to encrypt without encrypter at level "
             << QuicUtils::EncryptionLevelToString(level);
    RaiseError(QUIC_ENCRYPTION_FAILURE);
    return 0;
  }

  QuicStringPiece associated_data =
      packet.AssociatedData(version_.transport_version);
  // Copy in the header, because the encrypter only populates the encrypted
  // plaintext content.
  const size_t ad_len = associated_data.length();
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

bool QuicFramer::DecryptPayload(QuicStringPiece encrypted,
                                QuicStringPiece associated_data,
                                const QuicPacketHeader& header,
                                char* decrypted_buffer,
                                size_t buffer_length,
                                size_t* decrypted_length,
                                EncryptionLevel* decrypted_level) {
  if (!EncryptionLevelIsValid(decrypter_level_)) {
    QUIC_BUG << "Attempted to decrypt with bad decrypter_level_";
    return false;
  }
  EncryptionLevel level = decrypter_level_;
  QuicDecrypter* decrypter = decrypter_[level].get();
  QuicDecrypter* alternative_decrypter = nullptr;
  if (version().KnowsWhichDecrypterToUse()) {
    if (header.form == GOOGLE_QUIC_PACKET) {
      QUIC_BUG << "Attempted to decrypt GOOGLE_QUIC_PACKET with a version that "
                  "knows which decrypter to use";
      return false;
    }
    level = GetEncryptionLevel(header);
    if (!EncryptionLevelIsValid(level)) {
      QUIC_BUG << "Attempted to decrypt with bad level";
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
  } else if (alternative_decrypter_level_ != NUM_ENCRYPTION_LEVELS) {
    if (!EncryptionLevelIsValid(alternative_decrypter_level_)) {
      QUIC_BUG << "Attempted to decrypt with bad alternative_decrypter_level_";
      return false;
    }
    alternative_decrypter = decrypter_[alternative_decrypter_level_].get();
  }

  if (decrypter == nullptr) {
    QUIC_BUG << "Attempting to decrypt without decrypter, encryption level:"
             << level << " version:" << version();
    return false;
  }

  bool success = decrypter->DecryptPacket(
      header.packet_number.ToUint64(), associated_data, encrypted,
      decrypted_buffer, decrypted_length, buffer_length);
  if (success) {
    visitor_->OnDecryptedPacket(level);
    *decrypted_level = level;
  } else if (alternative_decrypter != nullptr) {
    if (header.nonce != nullptr) {
      DCHECK_EQ(perspective_, Perspective::IS_CLIENT);
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
        DCHECK(header.nonce == nullptr);
      }
    }

    if (try_alternative_decryption) {
      success = alternative_decrypter->DecryptPacket(
          header.packet_number.ToUint64(), associated_data, encrypted,
          decrypted_buffer, decrypted_length, buffer_length);
    }
    if (success) {
      visitor_->OnDecryptedPacket(alternative_decrypter_level_);
      *decrypted_level = decrypter_level_;
      if (alternative_decrypter_latch_) {
        if (!EncryptionLevelIsValid(alternative_decrypter_level_)) {
          QUIC_BUG << "Attempted to latch alternate decrypter with bad "
                      "alternative_decrypter_level_";
          return false;
        }
        // Switch to the alternative decrypter and latch so that we cannot
        // switch back.
        decrypter_level_ = alternative_decrypter_level_;
        alternative_decrypter_level_ = NUM_ENCRYPTION_LEVELS;
      } else {
        // Switch the alternative decrypter so that we use it first next time.
        EncryptionLevel level = alternative_decrypter_level_;
        alternative_decrypter_level_ = decrypter_level_;
        decrypter_level_ = level;
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

  // If |ecn_counters_populated| is true and any of the ecn counters is non-0
  // then the ecn counters are included...
  if (frame.ecn_counters_populated &&
      (frame.ect_0_count || frame.ect_1_count || frame.ecn_ce_count)) {
    ack_frame_size += QuicDataWriter::GetVarInt62Len(frame.ect_0_count);
    ack_frame_size += QuicDataWriter::GetVarInt62Len(frame.ect_1_count);
    ack_frame_size += QuicDataWriter::GetVarInt62Len(frame.ecn_ce_count);
  }

  // The rest (ack_block_count, first_ack_block, and additional ack
  // blocks, if any) depends:
  uint64_t ack_block_count = frame.packets.NumIntervals();
  if (ack_block_count == 0) {
    // If the QuicAckFrame has no Intervals, then it is interpreted
    // as an ack of a single packet at QuicAckFrame.largest_acked.
    // The resulting ack will consist of only the frame's
    // largest_ack & first_ack_block fields. The first ack block will be 0
    // (indicating a single packet) and the ack block_count will be 0.
    // Each 0 takes 1 byte when VarInt62 encoded.
    ack_frame_size += 2;
    return ack_frame_size;
  }

  auto itr = frame.packets.rbegin();
  QuicPacketNumber ack_block_largest = largest_acked;
  QuicPacketNumber ack_block_smallest;
  if ((itr->max() - 1) == largest_acked) {
    // If largest_acked + 1 is equal to the Max() of the first Interval
    // in the QuicAckFrame then the first Interval is the first ack block of the
    // frame; remaining Intervals are additional ack blocks.  The QuicAckFrame's
    // first Interval is encoded in the frame's largest_acked/first_ack_block,
    // the remaining Intervals are encoded in additional ack blocks in the
    // frame, and the packet's ack_block_count is the number of QuicAckFrame
    // Intervals - 1.
    ack_block_smallest = itr->min();
    itr++;
    ack_block_count--;
  } else {
    // If QuicAckFrame.largest_acked is NOT equal to the Max() of
    // the first Interval then it is interpreted as acking a single
    // packet at QuicAckFrame.largest_acked, with additional
    // Intervals indicating additional ack blocks. The encoding is
    //  a) The packet's largest_acked is the QuicAckFrame's largest
    //     acked,
    //  b) the first ack block size is 0,
    //  c) The packet's ack_block_count is the number of QuicAckFrame
    //     Intervals, and
    //  d) The QuicAckFrame Intervals are encoded in additional ack
    //     blocks in the packet.
    ack_block_smallest = largest_acked;
  }
  size_t ack_block_count_size = QuicDataWriter::GetVarInt62Len(ack_block_count);
  ack_frame_size += ack_block_count_size;

  uint64_t first_ack_block = ack_block_largest - ack_block_smallest;
  size_t first_ack_block_size = QuicDataWriter::GetVarInt62Len(first_ack_block);
  ack_frame_size += first_ack_block_size;

  // Account for the remaining Intervals, if any.
  while (ack_block_count != 0) {
    uint64_t gap_size = ack_block_smallest - itr->max();
    // Decrement per the protocol specification
    size_t size_of_gap_size = QuicDataWriter::GetVarInt62Len(gap_size - 1);
    ack_frame_size += size_of_gap_size;

    uint64_t block_size = itr->max() - itr->min();
    // Decrement per the protocol specification
    size_t size_of_block_size = QuicDataWriter::GetVarInt62Len(block_size - 1);
    ack_frame_size += size_of_block_size;

    ack_block_smallest = itr->min();
    itr++;
    ack_block_count--;
  }

  return ack_frame_size;
}

size_t QuicFramer::GetAckFrameSize(
    const QuicAckFrame& ack,
    QuicPacketNumberLength /*packet_number_length*/) {
  DCHECK(!ack.packets.Empty());
  size_t ack_size = 0;

  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return GetIetfAckFrameSize(ack);
  }
  AckFrameInfo ack_info = GetAckFrameInfo(ack);
  QuicPacketNumberLength largest_acked_length =
      GetMinPacketNumberLength(version_.transport_version, LargestAcked(ack));
  QuicPacketNumberLength ack_block_length = GetMinPacketNumberLength(
      version_.transport_version, QuicPacketNumber(ack_info.max_block_length));

  ack_size =
      GetMinAckFrameSize(version_.transport_version, largest_acked_length);
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
    const QuicFrame& frame,
    bool last_frame_in_packet,
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
      return GetStopWaitingFrameSize(version_.transport_version,
                                     packet_number_length);
    case MTU_DISCOVERY_FRAME:
      // MTU discovery frames are serialized as ping frames.
      return kQuicFrameTypeSize;
    case MESSAGE_FRAME:
      return GetMessageFrameSize(version_.transport_version,
                                 last_frame_in_packet,
                                 frame.message_frame->message_length);
    case PADDING_FRAME:
      DCHECK(false);
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
    return AppendIetfTypeByte(frame, last_frame_in_packet, writer);
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

bool QuicFramer::AppendIetfTypeByte(const QuicFrame& frame,
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
          set_detailed_error("Invalid QuicConnectionCloseFrame type.");
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
      if (frame.window_update_frame->stream_id ==
          QuicUtils::GetInvalidStreamId(transport_version())) {
        type_byte = IETF_MAX_DATA;
      } else {
        type_byte = IETF_MAX_STREAM_DATA;
      }
      break;
    case BLOCKED_FRAME:
      if (frame.blocked_frame->stream_id ==
          QuicUtils::GetInvalidStreamId(transport_version())) {
        type_byte = IETF_BLOCKED;
      } else {
        type_byte = IETF_STREAM_BLOCKED;
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
    default:
      QUIC_BUG << "Attempt to generate a frame type for an unsupported value: "
               << frame.type;
      return false;
  }
  return writer->WriteUInt8(type_byte);
}

// static
bool QuicFramer::AppendPacketNumber(QuicPacketNumberLength packet_number_length,
                                    QuicPacketNumber packet_number,
                                    QuicDataWriter* writer) {
  DCHECK(packet_number.IsInitialized());
  if (!IsValidPacketNumberLength(packet_number_length)) {
    QUIC_BUG << "Invalid packet_number_length: " << packet_number_length;
    return false;
  }
  return writer->WriteBytesToUInt64(packet_number_length,
                                    packet_number.ToUint64());
}

// static
bool QuicFramer::AppendStreamId(size_t stream_id_length,
                                QuicStreamId stream_id,
                                QuicDataWriter* writer) {
  if (stream_id_length == 0 || stream_id_length > 4) {
    QUIC_BUG << "Invalid stream_id_length: " << stream_id_length;
    return false;
  }
  return writer->WriteBytesToUInt64(stream_id_length, stream_id);
}

// static
bool QuicFramer::AppendStreamOffset(size_t offset_length,
                                    QuicStreamOffset offset,
                                    QuicDataWriter* writer) {
  if (offset_length == 1 || offset_length > 8) {
    QUIC_BUG << "Invalid stream_offset_length: " << offset_length;
    return false;
  }

  return writer->WriteBytesToUInt64(offset_length, offset);
}

// static
bool QuicFramer::AppendAckBlock(uint8_t gap,
                                QuicPacketNumberLength length_length,
                                uint64_t length,
                                QuicDataWriter* writer) {
  if (length == 0) {
    if (!IsValidPacketNumberLength(length_length)) {
      QUIC_BUG << "Invalid packet_number_length: " << length_length;
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
    QUIC_BUG << "Writing stream id size failed.";
    return false;
  }
  if (!AppendStreamOffset(
          GetStreamOffsetSize(version_.transport_version, frame.offset),
          frame.offset, writer)) {
    QUIC_BUG << "Writing offset size failed.";
    return false;
  }
  if (!no_stream_frame_length) {
    static_assert(
        std::numeric_limits<decltype(frame.data_length)>::max() <=
            std::numeric_limits<uint16_t>::max(),
        "If frame.data_length can hold more than a uint16_t than we need to "
        "check that frame.data_length <= std::numeric_limits<uint16_t>::max()");
    if (!writer->WriteUInt16(static_cast<uint16_t>(frame.data_length))) {
      QUIC_BUG << "Writing stream frame length failed";
      return false;
    }
  }

  if (data_producer_ != nullptr) {
    DCHECK_EQ(nullptr, frame.data_buffer);
    if (frame.data_length == 0) {
      return true;
    }
    if (data_producer_->WriteStreamData(frame.stream_id, frame.offset,
                                        frame.data_length,
                                        writer) != WRITE_SUCCESS) {
      QUIC_BUG << "Writing frame data failed.";
      return false;
    }
    return true;
  }

  if (!writer->WriteBytes(frame.data_buffer, frame.data_length)) {
    QUIC_BUG << "Writing frame data failed.";
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

  // TODO(ianswett): Don't use QuicStringPiece as an intermediary.
  QuicStringPiece data;
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
    DCHECK_EQ(nullptr, frame.data_buffer);

    if (data_producer_->WriteStreamData(frame.stream_id, frame.offset,
                                        frame.data_length,
                                        writer) != WRITE_SUCCESS) {
      set_detailed_error("Writing frame data failed.");
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
    DCHECK_EQ(nullptr, frame.data_buffer);
    if (!data_producer_->WriteCryptoData(frame.level, frame.offset,
                                         frame.data_length, writer)) {
      return false;
    }
  }
  return true;
}

void QuicFramer::set_version(const ParsedQuicVersion version) {
  DCHECK(IsSupportedVersion(version)) << ParsedQuicVersionToString(version);
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
      GetMinPacketNumberLength(version_.transport_version, largest_acked);
  QuicPacketNumberLength ack_block_length =
      GetMinPacketNumberLength(version_.transport_version,
                               QuicPacketNumber(new_ack_info.max_block_length));
  // Calculate available bytes for timestamps and ack blocks.
  int32_t available_timestamp_and_ack_block_bytes =
      writer->capacity() - writer->length() - ack_block_length -
      GetMinAckFrameSize(version_.transport_version, largest_acked_length) -
      (new_ack_info.num_ack_blocks != 0 ? kNumberOfAckBlocksSize : 0);
  DCHECK_LE(0, available_timestamp_and_ack_block_bytes);

  // Write out the type byte by setting the low order bits and doing shifts
  // to make room for the next bit flags to be set.
  // Whether there are multiple ack blocks.
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
    DCHECK_LE(0u, frame.ack_delay_time.ToMicroseconds());
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
        if (QUIC_PREDICT_FALSE(num_ack_blocks_written != num_ack_blocks)) {
          QUIC_BUG << "Wrote " << num_ack_blocks_written
                   << ", expected to write " << num_ack_blocks;
        }
        break;
      }

      const uint8_t last_gap =
          total_gap -
          (num_encoded_gaps - 1) * std::numeric_limits<uint8_t>::max();
      // Append the final ACK block with a non-empty size.
      if (!AppendAckBlock(last_gap, ack_block_length,
                          PacketNumberIntervalLength(interval), writer)) {
        return false;
      }
      ++num_ack_blocks_written;
    }
    DCHECK_EQ(num_ack_blocks, num_ack_blocks_written);
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
  DCHECK_GE(std::numeric_limits<uint8_t>::max(),
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

  DCHECK_GE(std::numeric_limits<uint8_t>::max(), delta_from_largest_observed);
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

bool QuicFramer::AppendStopWaitingFrame(const QuicPacketHeader& header,
                                        const QuicStopWaitingFrame& frame,
                                        QuicDataWriter* writer) {
  DCHECK(!VersionHasIetfInvariantHeader(version_.transport_version));
  DCHECK(frame.least_unacked.IsInitialized() &&
         header.packet_number >= frame.least_unacked);
  const uint64_t least_unacked_delta =
      header.packet_number - frame.least_unacked;
  const uint64_t length_shift = header.packet_number_length * 8;

  if (least_unacked_delta >> length_shift > 0) {
    QUIC_BUG << "packet_number_length " << header.packet_number_length
             << " is too small for least_unacked_delta: " << least_unacked_delta
             << " packet_number:" << header.packet_number
             << " least_unacked:" << frame.least_unacked
             << " version:" << version_.transport_version;
    return false;
  }
  if (least_unacked_delta == 0) {
    return writer->WriteBytesToUInt64(header.packet_number_length,
                                      least_unacked_delta);
  }
  if (!AppendPacketNumber(header.packet_number_length,
                          QuicPacketNumber(least_unacked_delta), writer)) {
    QUIC_BUG << " seq failed: " << header.packet_number_length;
    return false;
  }

  return true;
}

int QuicFramer::CalculateIetfAckBlockCount(const QuicAckFrame& frame,
                                           QuicDataWriter* /*writer*/,
                                           size_t available_space) {
  // Number of blocks requested in the frame
  uint64_t ack_block_count = frame.packets.NumIntervals();

  auto itr = frame.packets.rbegin();

  int actual_block_count = 1;
  uint64_t block_length = itr->max() - itr->min();
  size_t encoded_size = QuicDataWriter::GetVarInt62Len(block_length);
  if (encoded_size > available_space) {
    return 0;
  }
  available_space -= encoded_size;
  QuicPacketNumber previous_ack_end = itr->min();
  ack_block_count--;

  while (ack_block_count) {
    // Each block is a gap followed by another ACK. Calculate each value,
    // determine the encoded lengths, and check against the available space.
    itr++;
    size_t gap = previous_ack_end - itr->max() - 1;
    encoded_size = QuicDataWriter::GetVarInt62Len(gap);

    // Add the ACK block.
    block_length = itr->max() - itr->min();
    encoded_size += QuicDataWriter::GetVarInt62Len(block_length);

    if (encoded_size > available_space) {
      // No room for this block, so what we've
      // done up to now is all that can be done.
      return actual_block_count;
    }
    available_space -= encoded_size;
    actual_block_count++;
    previous_ack_end = itr->min();
    ack_block_count--;
  }
  // Ran through the whole thing! We can do all blocks.
  return actual_block_count;
}

bool QuicFramer::AppendIetfAckFrameAndTypeByte(const QuicAckFrame& frame,
                                               QuicDataWriter* writer) {
  // Assume frame is an IETF_ACK frame. If |ecn_counters_populated| is true and
  // any of the ECN counters is non-0 then turn it into an IETF_ACK+ECN frame.
  uint8_t type = IETF_ACK;
  if (frame.ecn_counters_populated &&
      (frame.ect_0_count || frame.ect_1_count || frame.ecn_ce_count)) {
    type = IETF_ACK_ECN;
  }

  if (!writer->WriteUInt8(type)) {
    set_detailed_error("No room for frame-type");
    return false;
  }

  QuicPacketNumber largest_acked = LargestAcked(frame);
  if (!writer->WriteVarInt62(largest_acked.ToUint64())) {
    set_detailed_error("No room for largest-acked in ack frame");
    return false;
  }

  uint64_t ack_delay_time_us = kVarInt62MaxValue;
  if (!frame.ack_delay_time.IsInfinite()) {
    DCHECK_LE(0u, frame.ack_delay_time.ToMicroseconds());
    ack_delay_time_us = frame.ack_delay_time.ToMicroseconds();
    ack_delay_time_us = ack_delay_time_us >> local_ack_delay_exponent_;
  }

  if (!writer->WriteVarInt62(ack_delay_time_us)) {
    set_detailed_error("No room for ack-delay in ack frame");
    return false;
  }
  if (type == IETF_ACK_ECN) {
    // Encode the ACK ECN fields
    if (!writer->WriteVarInt62(frame.ect_0_count)) {
      set_detailed_error("No room for ect_0_count in ack frame");
      return false;
    }
    if (!writer->WriteVarInt62(frame.ect_1_count)) {
      set_detailed_error("No room for ect_1_count in ack frame");
      return false;
    }
    if (!writer->WriteVarInt62(frame.ecn_ce_count)) {
      set_detailed_error("No room for ecn_ce_count in ack frame");
      return false;
    }
  }

  uint64_t ack_block_count = frame.packets.NumIntervals();
  if (ack_block_count == 0) {
    // If the QuicAckFrame has no Intervals, then it is interpreted
    // as an ack of a single packet at QuicAckFrame.largest_acked.
    // The resulting ack will consist of only the frame's
    // largest_ack & first_ack_block fields. The first ack block will be 0
    // (indicating a single packet) and the ack block_count will be 0.
    if (!writer->WriteVarInt62(0)) {
      set_detailed_error("No room for ack block count in ack frame");
      return false;
    }
    // size of the first block is 1 packet
    if (!writer->WriteVarInt62(0)) {
      set_detailed_error("No room for first ack block in ack frame");
      return false;
    }
    return true;
  }
  // Case 2 or 3
  auto itr = frame.packets.rbegin();

  QuicPacketNumber ack_block_largest(largest_acked);
  QuicPacketNumber ack_block_smallest;
  if ((itr->max() - 1) == QuicPacketNumber(largest_acked)) {
    // If largest_acked + 1 is equal to the Max() of the first Interval
    // in the QuicAckFrame then the first Interval is the first ack block of the
    // frame; remaining Intervals are additional ack blocks.  The QuicAckFrame's
    // first Interval is encoded in the frame's largest_acked/first_ack_block,
    // the remaining Intervals are encoded in additional ack blocks in the
    // frame, and the packet's ack_block_count is the number of QuicAckFrame
    // Intervals - 1.
    ack_block_smallest = itr->min();
    itr++;
    ack_block_count--;
  } else {
    // If QuicAckFrame.largest_acked is NOT equal to the Max() of
    // the first Interval then it is interpreted as acking a single
    // packet at QuicAckFrame.largest_acked, with additional
    // Intervals indicating additional ack blocks. The encoding is
    //  a) The packet's largest_acked is the QuicAckFrame's largest
    //     acked,
    //  b) the first ack block size is 0,
    //  c) The packet's ack_block_count is the number of QuicAckFrame
    //     Intervals, and
    //  d) The QuicAckFrame Intervals are encoded in additional ack
    //     blocks in the packet.
    ack_block_smallest = largest_acked;
  }

  if (!writer->WriteVarInt62(ack_block_count)) {
    set_detailed_error("No room for ack block count in ack frame");
    return false;
  }

  uint64_t first_ack_block = ack_block_largest - ack_block_smallest;
  if (!writer->WriteVarInt62(first_ack_block)) {
    set_detailed_error("No room for first ack block in ack frame");
    return false;
  }

  // For the remaining QuicAckFrame Intervals, if any
  while (ack_block_count != 0) {
    uint64_t gap_size = ack_block_smallest - itr->max();
    if (!writer->WriteVarInt62(gap_size - 1)) {
      set_detailed_error("No room for gap block in ack frame");
      return false;
    }

    uint64_t block_size = itr->max() - itr->min();
    if (!writer->WriteVarInt62(block_size - 1)) {
      set_detailed_error("No room for nth ack block in ack frame");
      return false;
    }

    ack_block_smallest = itr->min();
    itr++;
    ack_block_count--;
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
    const QuicConnectionCloseFrame& frame,
    QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    return AppendIetfConnectionCloseFrame(frame, writer);
  }
  uint32_t error_code = static_cast<uint32_t>(frame.quic_error_code);
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
  if (!writer->WriteUInt64(frame.byte_offset)) {
    return false;
  }
  return true;
}

bool QuicFramer::AppendBlockedFrame(const QuicBlockedFrame& frame,
                                    QuicDataWriter* writer) {
  if (VersionHasIetfQuicFrames(version_.transport_version)) {
    if (frame.stream_id == QuicUtils::GetInvalidStreamId(transport_version())) {
      return AppendIetfBlockedFrame(frame, writer);
    }
    return AppendStreamBlockedFrame(frame, writer);
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
    QUIC_BUG_IF(frame.num_padding_bytes != -1);
    writer->WritePadding();
    return true;
  }
  // Please note, num_padding_bytes includes type byte which has been written.
  return writer->WritePaddingBytes(frame.num_padding_bytes - 1);
}

bool QuicFramer::AppendMessageFrameAndTypeByte(const QuicMessageFrame& frame,
                                               bool last_frame_in_packet,
                                               QuicDataWriter* writer) {
  uint8_t type_byte = last_frame_in_packet ? IETF_EXTENSION_MESSAGE_NO_LENGTH
                                           : IETF_EXTENSION_MESSAGE;
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

bool QuicFramer::IsVersionNegotiation(
    const QuicPacketHeader& header,
    bool packet_has_ietf_packet_header) const {
  if (!packet_has_ietf_packet_header &&
      perspective_ == Perspective::IS_CLIENT) {
    return header.version_flag;
  }
  if (header.form == IETF_QUIC_SHORT_HEADER_PACKET) {
    return false;
  }
  return header.long_packet_type == VERSION_NEGOTIATION;
}

bool QuicFramer::AppendIetfConnectionCloseFrame(
    const QuicConnectionCloseFrame& frame,
    QuicDataWriter* writer) {
  if (frame.close_type != IETF_QUIC_TRANSPORT_CONNECTION_CLOSE &&
      frame.close_type != IETF_QUIC_APPLICATION_CONNECTION_CLOSE) {
    QUIC_BUG << "Invalid close_type for writing IETF CONNECTION CLOSE.";
    set_detailed_error("Invalid close_type for writing IETF CONNECTION CLOSE.");
    return false;
  }

  uint64_t close_code = 0;
  if (frame.close_type == IETF_QUIC_TRANSPORT_CONNECTION_CLOSE) {
    close_code = static_cast<uint64_t>(frame.transport_error_code);
  } else if (frame.close_type == IETF_QUIC_APPLICATION_CONNECTION_CLOSE) {
    close_code = static_cast<uint64_t>(frame.application_error_code);
  }

  if (!writer->WriteVarInt62(close_code)) {
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
      GenerateErrorString(frame.error_details, frame.extracted_error_code);
  if (!writer->WriteStringPieceVarInt62(
          TruncateErrorString(final_error_string))) {
    set_detailed_error("Can not write connection close phrase");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessIetfConnectionCloseFrame(
    QuicDataReader* reader,
    QuicConnectionCloseType type,
    QuicConnectionCloseFrame* frame) {
  frame->close_type = type;
  uint64_t error_code;

  if (!reader->ReadVarInt62(&error_code)) {
    set_detailed_error("Unable to read connection close error code.");
    return false;
  }

  // TODO(fkastenholz): When error codes uniformly go to uint64, remove the
  // range check.
  if (frame->close_type == IETF_QUIC_TRANSPORT_CONNECTION_CLOSE) {
    if (error_code > 0xffff) {
      frame->transport_error_code =
          static_cast<QuicIetfTransportErrorCodes>(0xffff);
      QUIC_DLOG(ERROR) << "Transport error code " << error_code << " > 0xffff";
    } else {
      frame->transport_error_code =
          static_cast<QuicIetfTransportErrorCodes>(error_code);
    }
  } else if (frame->close_type == IETF_QUIC_APPLICATION_CONNECTION_CLOSE) {
    if (error_code > 0xffff) {
      frame->application_error_code = 0xffff;
      QUIC_DLOG(ERROR) << "Application error code " << error_code
                       << " > 0xffff";
    } else {
      frame->application_error_code = static_cast<uint16_t>(error_code);
    }
  }

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

  QuicStringPiece phrase;
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
  if (!reader->ReadVarIntU32(&frame->stream_id)) {
    set_detailed_error("Unable to read rst stream stream id.");
    return false;
  }

  uint64_t error_code;
  if (!reader->ReadVarInt62(&error_code)) {
    set_detailed_error("Unable to read rst stream error code.");
    return false;
  }
  if (error_code > 0xffff) {
    frame->ietf_error_code = 0xffff;
    QUIC_DLOG(ERROR) << "Reset stream error code (" << error_code
                     << ") > 0xffff";
  } else {
    frame->ietf_error_code = static_cast<uint16_t>(error_code);
  }

  if (!reader->ReadVarInt62(&frame->byte_offset)) {
    set_detailed_error("Unable to read rst stream sent byte offset.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessStopSendingFrame(
    QuicDataReader* reader,
    QuicStopSendingFrame* stop_sending_frame) {
  if (!reader->ReadVarIntU32(&stop_sending_frame->stream_id)) {
    set_detailed_error("Unable to read stop sending stream id.");
    return false;
  }

  uint64_t error_code;
  if (!reader->ReadVarInt62(&error_code)) {
    set_detailed_error("Unable to read stop sending application error code.");
    return false;
  }
  // TODO(fkastenholz): when error codes go to uint64_t, remove this.
  if (error_code > 0xffff) {
    stop_sending_frame->application_error_code = 0xffff;
    QUIC_DLOG(ERROR) << "Stop sending error code (" << error_code
                     << ") > 0xffff";
  } else {
    stop_sending_frame->application_error_code =
        static_cast<uint16_t>(error_code);
  }
  return true;
}

bool QuicFramer::AppendStopSendingFrame(
    const QuicStopSendingFrame& stop_sending_frame,
    QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(stop_sending_frame.stream_id)) {
    set_detailed_error("Can not write stop sending stream id");
    return false;
  }
  if (!writer->WriteVarInt62(
          static_cast<uint64_t>(stop_sending_frame.application_error_code))) {
    set_detailed_error("Can not write application error code");
    return false;
  }
  return true;
}

// Append/process IETF-Format MAX_DATA Frame
bool QuicFramer::AppendMaxDataFrame(const QuicWindowUpdateFrame& frame,
                                    QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.byte_offset)) {
    set_detailed_error("Can not write MAX_DATA byte-offset");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessMaxDataFrame(QuicDataReader* reader,
                                     QuicWindowUpdateFrame* frame) {
  frame->stream_id = QuicUtils::GetInvalidStreamId(transport_version());
  if (!reader->ReadVarInt62(&frame->byte_offset)) {
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
  if (!writer->WriteVarInt62(frame.byte_offset)) {
    set_detailed_error("Can not write MAX_STREAM_DATA byte-offset");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessMaxStreamDataFrame(QuicDataReader* reader,
                                           QuicWindowUpdateFrame* frame) {
  if (!reader->ReadVarIntU32(&frame->stream_id)) {
    set_detailed_error("Can not read MAX_STREAM_DATA stream id");
    return false;
  }
  if (!reader->ReadVarInt62(&frame->byte_offset)) {
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
  if (!reader->ReadVarIntU32(&frame->stream_count)) {
    set_detailed_error("Can not read MAX_STREAMS stream count.");
    return false;
  }
  frame->unidirectional = (frame_type == IETF_MAX_STREAMS_UNIDIRECTIONAL);
  return true;
}

bool QuicFramer::AppendIetfBlockedFrame(const QuicBlockedFrame& frame,
                                        QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.offset)) {
    set_detailed_error("Can not write blocked offset.");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessIetfBlockedFrame(QuicDataReader* reader,
                                         QuicBlockedFrame* frame) {
  // Indicates that it is a BLOCKED frame (as opposed to STREAM_BLOCKED).
  frame->stream_id = QuicUtils::GetInvalidStreamId(transport_version());
  if (!reader->ReadVarInt62(&frame->offset)) {
    set_detailed_error("Can not read blocked offset.");
    return false;
  }
  return true;
}

bool QuicFramer::AppendStreamBlockedFrame(const QuicBlockedFrame& frame,
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

bool QuicFramer::ProcessStreamBlockedFrame(QuicDataReader* reader,
                                           QuicBlockedFrame* frame) {
  if (!reader->ReadVarIntU32(&frame->stream_id)) {
    set_detailed_error("Can not read stream blocked stream id.");
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
  if (!reader->ReadVarIntU32(&frame->stream_count)) {
    set_detailed_error("Can not read STREAMS_BLOCKED stream count.");
    return false;
  }
  frame->unidirectional = (frame_type == IETF_STREAMS_BLOCKED_UNIDIRECTIONAL);

  // TODO(fkastenholz): handle properly when the STREAMS_BLOCKED
  // frame is implemented and passed up to the stream ID manager.
  if (frame->stream_count >
      QuicUtils::GetMaxStreamCount(
          (frame_type == IETF_STREAMS_BLOCKED_UNIDIRECTIONAL),
          ((perspective_ == Perspective::IS_CLIENT)
               ? Perspective::IS_SERVER
               : Perspective::IS_CLIENT))) {
    // If stream count is such that the resulting stream ID would exceed our
    // implementation limit, generate an error.
    set_detailed_error(
        "STREAMS_BLOCKED stream count exceeds implementation limit.");
    return false;
  }
  return true;
}

bool QuicFramer::AppendNewConnectionIdFrame(
    const QuicNewConnectionIdFrame& frame,
    QuicDataWriter* writer) {
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
    const QuicRetireConnectionIdFrame& frame,
    QuicDataWriter* writer) {
  if (!writer->WriteVarInt62(frame.sequence_number)) {
    set_detailed_error("Can not write Retire Connection ID sequence number");
    return false;
  }
  return true;
}

bool QuicFramer::ProcessRetireConnectionIdFrame(
    QuicDataReader* reader,
    QuicRetireConnectionIdFrame* frame) {
  if (!reader->ReadVarInt62(&frame->sequence_number)) {
    set_detailed_error(
        "Unable to read retire connection ID frame sequence number.");
    return false;
  }
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
  const size_t offset_len =
      GetStreamOffsetSize(version_.transport_version, frame.offset);
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
    const QuicStreamFrame& frame,
    bool last_frame_in_packet) const {
  DCHECK(VersionHasIetfQuicFrames(version_.transport_version));
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

void QuicFramer::InferPacketHeaderTypeFromVersion() {
  // This function should only be called when server connection negotiates the
  // version.
  DCHECK(perspective_ == Perspective::IS_SERVER &&
         !infer_packet_header_type_from_version_);
  infer_packet_header_type_from_version_ = true;
}

void QuicFramer::EnableMultiplePacketNumberSpacesSupport() {
  if (supports_multiple_packet_number_spaces_) {
    QUIC_BUG << "Multiple packet number spaces has already been enabled";
    return;
  }
  if (largest_packet_number_.IsInitialized()) {
    QUIC_BUG << "Try to enable multiple packet number spaces support after any "
                "packet has been received.";
    return;
  }

  supports_multiple_packet_number_spaces_ = true;
}

// static
QuicErrorCode QuicFramer::ProcessPacketDispatcher(
    const QuicEncryptedPacket& packet,
    uint8_t expected_destination_connection_id_length,
    PacketHeaderFormat* format,
    bool* version_flag,
    QuicVersionLabel* version_label,
    QuicConnectionId* destination_connection_id,
    QuicConnectionId* source_connection_id,
    std::string* detailed_error) {
  DCHECK(!GetQuicReloadableFlag(quic_use_parse_public_header));
  QuicDataReader reader(packet.data(), packet.length());

  *source_connection_id = EmptyQuicConnectionId();
  uint8_t first_byte;
  if (!reader.ReadBytes(&first_byte, 1)) {
    *detailed_error = "Unable to read first byte.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  uint8_t destination_connection_id_length = 0, source_connection_id_length = 0;
  if (!QuicUtils::IsIetfPacketHeader(first_byte)) {
    *format = GOOGLE_QUIC_PACKET;
    *version_flag = (first_byte & PACKET_PUBLIC_FLAGS_VERSION) != 0;
    destination_connection_id_length =
        first_byte & PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID;
    if (destination_connection_id_length == 0 ||
        !reader.ReadConnectionId(destination_connection_id,
                                 destination_connection_id_length)) {
      *detailed_error = "Unable to read ConnectionId.";
      return QUIC_INVALID_PACKET_HEADER;
    }
    if (*version_flag && !ProcessVersionLabel(&reader, version_label)) {
      *detailed_error = "Unable to read protocol version.";
      return QUIC_INVALID_PACKET_HEADER;
    }
    return QUIC_NO_ERROR;
  }

  *format = GetIetfPacketHeaderFormat(first_byte);
  QUIC_DVLOG(1) << "Dispatcher: Processing IETF QUIC packet, format: "
                << *format;
  *version_flag = *format == IETF_QUIC_LONG_HEADER_PACKET;
  if (*format == IETF_QUIC_LONG_HEADER_PACKET) {
    if (!ProcessVersionLabel(&reader, version_label)) {
      *detailed_error = "Unable to read protocol version.";
      return QUIC_INVALID_PACKET_HEADER;
    }
    // Set should_update_expected_server_connection_id_length to true to bypass
    // connection ID lengths validation.
    uint8_t unused_expected_server_connection_id_length = 0;
    if (!ProcessAndValidateIetfConnectionIdLength(
            &reader, ParseQuicVersionLabel(*version_label),
            Perspective::IS_SERVER,
            /*should_update_expected_server_connection_id_length=*/true,
            &unused_expected_server_connection_id_length,
            &destination_connection_id_length, &source_connection_id_length,
            detailed_error)) {
      return QUIC_INVALID_PACKET_HEADER;
    }
  } else {
    // For short header packets, expected_destination_connection_id_length
    // is used to determine the destination_connection_id_length.
    destination_connection_id_length =
        expected_destination_connection_id_length;
    DCHECK_EQ(0, source_connection_id_length);
  }
  // Read destination connection ID.
  if (!reader.ReadConnectionId(destination_connection_id,
                               destination_connection_id_length)) {
    *detailed_error = "Unable to read destination connection ID.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  // Read source connection ID.
  if (!reader.ReadConnectionId(source_connection_id,
                               source_connection_id_length)) {
    *detailed_error = "Unable to read source connection ID.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  return QUIC_NO_ERROR;
}

// static
QuicErrorCode QuicFramer::ParsePublicHeaderDispatcher(
    const QuicEncryptedPacket& packet,
    uint8_t expected_destination_connection_id_length,
    PacketHeaderFormat* format,
    bool* version_present,
    bool* has_length_prefix,
    QuicVersionLabel* version_label,
    ParsedQuicVersion* parsed_version,
    QuicConnectionId* destination_connection_id,
    QuicConnectionId* source_connection_id,
    bool* retry_token_present,
    QuicStringPiece* retry_token,
    std::string* detailed_error) {
  QuicDataReader reader(packet.data(), packet.length());
  if (reader.IsDoneReading()) {
    *detailed_error = "Unable to read first byte.";
    return QUIC_INVALID_PACKET_HEADER;
  }
  const uint8_t first_byte = reader.PeekByte();
  const bool ietf_format = QuicUtils::IsIetfPacketHeader(first_byte);
  uint8_t unused_first_byte;
  QuicVariableLengthIntegerLength retry_token_length_length;
  QuicLongHeaderType unused_log_packet_type;
  const QuicErrorCode error_code = ParsePublicHeader(
      &reader, expected_destination_connection_id_length, ietf_format,
      &unused_first_byte, format, version_present, has_length_prefix,
      version_label, parsed_version, destination_connection_id,
      source_connection_id, &unused_log_packet_type, &retry_token_length_length,
      retry_token, detailed_error);
  *retry_token_present =
      retry_token_length_length != VARIABLE_LENGTH_INTEGER_LENGTH_0;
  return error_code;
}

// static
QuicErrorCode QuicFramer::ParsePublicHeaderGoogleQuic(
    QuicDataReader* reader,
    uint8_t* first_byte,
    PacketHeaderFormat* format,
    bool* version_present,
    QuicVersionLabel* version_label,
    ParsedQuicVersion* parsed_version,
    QuicConnectionId* destination_connection_id,
    std::string* detailed_error) {
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

inline bool PacketHasLengthPrefixedConnectionIds(
    const QuicDataReader& reader,
    ParsedQuicVersion parsed_version,
    QuicVersionLabel version_label,
    uint8_t first_byte) {
  if (parsed_version.transport_version != QUIC_VERSION_UNSUPPORTED) {
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
      connection_id_length_byte >= 0x20 && version_label == 0x50524F58) {
    return false;
  }

  return true;
}

inline bool ParseLongHeaderConnectionIds(
    QuicDataReader* reader,
    bool has_length_prefix,
    QuicConnectionId* destination_connection_id,
    QuicConnectionId* source_connection_id,
    std::string* detailed_error) {
  if (has_length_prefix) {
    if (!reader->ReadLengthPrefixedConnectionId(destination_connection_id)) {
      *detailed_error = "Unable to read destination connection ID.";
      return false;
    }
    if (!reader->ReadLengthPrefixedConnectionId(source_connection_id)) {
      *detailed_error = "Unable to read source connection ID.";
      return false;
    }
  } else {
    // Parse connection ID lengths.
    uint8_t connection_id_lengths_byte;
    if (!reader->ReadUInt8(&connection_id_lengths_byte)) {
      *detailed_error = "Unable to read connection ID lengths.";
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
    if (!reader->ReadConnectionId(destination_connection_id,
                                  destination_connection_id_length)) {
      *detailed_error = "Unable to read destination connection ID.";
      return false;
    }

    // Read source connection ID.
    if (!reader->ReadConnectionId(source_connection_id,
                                  source_connection_id_length)) {
      *detailed_error = "Unable to read source connection ID.";
      return false;
    }
  }
  return true;
}

}  // namespace

// static
QuicErrorCode QuicFramer::ParsePublicHeader(
    QuicDataReader* reader,
    uint8_t expected_destination_connection_id_length,
    bool ietf_format,
    uint8_t* first_byte,
    PacketHeaderFormat* format,
    bool* version_present,
    bool* has_length_prefix,
    QuicVersionLabel* version_label,
    ParsedQuicVersion* parsed_version,
    QuicConnectionId* destination_connection_id,
    QuicConnectionId* source_connection_id,
    QuicLongHeaderType* long_packet_type,
    QuicVariableLengthIntegerLength* retry_token_length_length,
    QuicStringPiece* retry_token,
    std::string* detailed_error) {
  *version_present = false;
  *has_length_prefix = false;
  *version_label = 0;
  *parsed_version = UnsupportedQuicVersion();
  *source_connection_id = EmptyQuicConnectionId();
  *long_packet_type = INVALID_PACKET_TYPE;
  *retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_0;
  *retry_token = QuicStringPiece();
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
    // Read destination connection ID using
    // expected_destination_connection_id_length to determine its length.
    if (!reader->ReadConnectionId(destination_connection_id,
                                  expected_destination_connection_id_length)) {
      *detailed_error = "Unable to read destination connection ID.";
      return QUIC_INVALID_PACKET_HEADER;
    }
    return QUIC_NO_ERROR;
  }

  DCHECK_EQ(IETF_QUIC_LONG_HEADER_PACKET, *format);
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
  if (!ParseLongHeaderConnectionIds(reader, *has_length_prefix,
                                    destination_connection_id,
                                    source_connection_id, detailed_error)) {
    return QUIC_INVALID_PACKET_HEADER;
  }

  if (parsed_version->transport_version == QUIC_VERSION_UNSUPPORTED) {
    // Skip parsing of long packet type and retry token for unknown versions.
    return QUIC_NO_ERROR;
  }

  // Parse long packet type.
  if (!GetLongHeaderType(*first_byte, long_packet_type)) {
    *detailed_error = "Unable to parse long packet type.";
    return QUIC_INVALID_PACKET_HEADER;
  }

  if (!parsed_version->SupportsRetry() || *long_packet_type != INITIAL) {
    // Retry token is only present on initial packets for some versions.
    return QUIC_NO_ERROR;
  }

  *retry_token_length_length = reader->PeekVarInt62Length();
  uint64_t retry_token_length;
  if (!reader->ReadVarInt62(&retry_token_length)) {
    *retry_token_length_length = VARIABLE_LENGTH_INTEGER_LENGTH_0;
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
    char* packet_bytes,
    QuicByteCount packet_length,
    const char* destination_connection_id_bytes,
    uint8_t destination_connection_id_length) {
  if (packet_bytes == nullptr) {
    QUIC_BUG << "Invalid packet_bytes";
    return false;
  }
  if (packet_length < kMinPacketSizeForVersionNegotiation ||
      packet_length > 65535) {
    QUIC_BUG << "Invalid packet_length";
    return false;
  }
  if (destination_connection_id_length > kQuicMaxConnectionId4BitLength ||
      destination_connection_id_length <
          kQuicMinimumInitialConnectionIdLength) {
    QUIC_BUG << "Invalid connection_id_length";
    return false;
  }
  const bool use_length_prefix =
      GetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids);
  const uint8_t last_version_byte = use_length_prefix ? 0xda : 0xba;
  // clang-format off
  const unsigned char packet_start_bytes[] = {
    // IETF long header with fixed bit set, type initial, all-0 encrypted bits.
    0xc0,
    // Version, part of the IETF space reserved for negotiation.
    // This intentionally differs from QuicVersionReservedForNegotiation()
    // to allow differentiating them over the wire.
    0xca, 0xba, 0xda, last_version_byte,
  };
  // clang-format on
  static_assert(sizeof(packet_start_bytes) == 5, "bad packet_start_bytes size");
  QuicDataWriter writer(packet_length, packet_bytes);
  if (!writer.WriteBytes(packet_start_bytes, sizeof(packet_start_bytes))) {
    QUIC_BUG << "Failed to write packet start";
    return false;
  }

  QuicConnectionId destination_connection_id(destination_connection_id_bytes,
                                             destination_connection_id_length);
  if (!AppendIetfConnectionIds(
          /*version_flag=*/true, use_length_prefix, destination_connection_id,
          EmptyQuicConnectionId(), &writer)) {
    QUIC_BUG << "Failed to write connection IDs";
    return false;
  }
  // Add 8 bytes of zeroes followed by 8 bytes of ones to ensure that this does
  // not parse with any known version. The zeroes make sure that packet numbers,
  // retry token lengths and payload lengths are parsed as zero, and if the
  // zeroes are treated as padding frames, 0xff is known to not parse as a
  // valid frame type.
  if (!writer.WriteUInt64(0) ||
      !writer.WriteUInt64(std::numeric_limits<uint64_t>::max())) {
    QUIC_BUG << "Failed to write 18 bytes";
    return false;
  }
  // Make sure the polite greeting below is padded to a 16-byte boundary to
  // make it easier to read in tcpdump.
  while (writer.length() % 16 != 0) {
    if (!writer.WriteUInt8(0)) {
      QUIC_BUG << "Failed to write padding byte";
      return false;
    }
  }
  // Add a polite greeting in case a human sees this in tcpdump.
  static const char polite_greeting[] =
      "This packet only exists to trigger IETF QUIC version negotiation. "
      "Please respond with a Version Negotiation packet indicating what "
      "versions you support. Thank you and have a nice day.";
  if (!writer.WriteBytes(polite_greeting, sizeof(polite_greeting))) {
    QUIC_BUG << "Failed to write polite greeting";
    return false;
  }
  // Fill the rest of the packet with zeroes.
  writer.WritePadding();
  DCHECK_EQ(0u, writer.remaining());
  return true;
}

// static
bool QuicFramer::ParseServerVersionNegotiationProbeResponse(
    const char* packet_bytes,
    QuicByteCount packet_length,
    char* source_connection_id_bytes,
    uint8_t* source_connection_id_length_out,
    std::string* detailed_error) {
  if (detailed_error == nullptr) {
    QUIC_BUG << "Invalid error_details";
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
  const bool use_length_prefix =
      GetQuicFlag(FLAGS_quic_prober_uses_length_prefixed_connection_ids);
  QuicConnectionId destination_connection_id, source_connection_id;
  if (use_length_prefix) {
    if (!reader.ReadLengthPrefixedConnectionId(&destination_connection_id)) {
      *detailed_error = "Failed to read destination connection ID";
      return false;
    }
    if (!reader.ReadLengthPrefixedConnectionId(&source_connection_id)) {
      *detailed_error = "Failed to read source connection ID";
      return false;
    }
  } else {
    uint8_t expected_server_connection_id_length = 0,
            destination_connection_id_length = 0,
            source_connection_id_length = 0;
    if (!ProcessAndValidateIetfConnectionIdLength(
            &reader, UnsupportedQuicVersion(), Perspective::IS_CLIENT,
            /*should_update_expected_server_connection_id_length=*/true,
            &expected_server_connection_id_length,
            &destination_connection_id_length, &source_connection_id_length,
            detailed_error)) {
      return false;
    }
    if (!reader.ReadConnectionId(&destination_connection_id,
                                 destination_connection_id_length)) {
      *detailed_error = "Failed to read destination connection ID";
      return false;
    }
    if (!reader.ReadConnectionId(&source_connection_id,
                                 source_connection_id_length)) {
      *detailed_error = "Failed to read source connection ID";
      return false;
    }
  }

  if (destination_connection_id.length() != 0) {
    *detailed_error = "Received unexpected destination connection ID length";
    return false;
  }

  if (!use_length_prefix && source_connection_id.length() == 0) {
    // We received a bad response due to b/139330014.
    // Reparse the packet assuming length prefixes.
    // This is a temporary client-side workaround until cl/263172621 is
    // deployed on production servers.
    // TODO(dschinazi): remove this client-side workaround once the server-side
    // fix is deployed.
    QuicDataReader reader2(packet_bytes, packet_length);
    uint8_t type_byte2 = 0;
    uint32_t version2 = 0;
    QuicConnectionId destination_connection_id2, source_connection_id2;
    if (reader2.ReadUInt8(&type_byte2) && reader2.ReadUInt32(&version2) &&
        reader2.ReadLengthPrefixedConnectionId(&destination_connection_id2) &&
        reader2.ReadLengthPrefixedConnectionId(&source_connection_id2) &&
        (type_byte2 & 0x80) != 0 && version2 == 0 &&
        destination_connection_id2.length() == 0 &&
        source_connection_id2.length() != 0) {
      source_connection_id = source_connection_id2;
    }
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
  std::vector<QuicStringPiece> ed =
      QuicTextUtils::Split(frame->error_details, ':');
  uint64_t extracted_error_code;
  if (ed.size() < 2 || !QuicTextUtils::IsAllDigits(ed[0]) ||
      !QuicTextUtils::StringToUint64(ed[0], &extracted_error_code)) {
    frame->extracted_error_code = QUIC_IETF_GQUIC_ERROR_MISSING;
    return;
  }
  // Return the error code (numeric) and the error details string without the
  // error code prefix. Note that Split returns everything up to, but not
  // including, the split character, so the length of ed[0] is just the number
  // of digits in the error number. In removing the prefix, 1 is added to the
  // length to account for the :
  QuicStringPiece x = QuicStringPiece(frame->error_details);
  x.remove_prefix(ed[0].length() + 1);
  frame->error_details = std::string(x);
  frame->extracted_error_code =
      static_cast<QuicErrorCode>(extracted_error_code);
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
