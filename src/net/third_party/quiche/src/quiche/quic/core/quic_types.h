// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_TYPES_H_
#define QUICHE_QUIC_CORE_QUIC_TYPES_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <ostream>
#include <vector>

#include "absl/container/inlined_vector.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packet_number.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/quiche_endian.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

using QuicPacketLength = uint16_t;
using QuicControlFrameId = uint32_t;
using QuicMessageId = uint32_t;

// IMPORTANT: IETF QUIC defines stream IDs and stream counts as being unsigned
// 62-bit numbers. However, we have decided to only support up to 2^32-1 streams
// in order to reduce the size of data structures such as QuicStreamFrame
// and QuicTransmissionInfo, as that allows them to fit in cache lines and has
// visible perfomance impact.
using QuicStreamId = uint32_t;

// Count of stream IDs. Used in MAX_STREAMS and STREAMS_BLOCKED frames.
using QuicStreamCount = QuicStreamId;

using QuicByteCount = uint64_t;
using QuicPacketCount = uint64_t;
using QuicPublicResetNonceProof = uint64_t;
using QuicStreamOffset = uint64_t;
using DiversificationNonce = std::array<char, 32>;
using PacketTimeVector = std::vector<std::pair<QuicPacketNumber, QuicTime>>;

enum : size_t { kStatelessResetTokenLength = 16 };
using StatelessResetToken = std::array<char, kStatelessResetTokenLength>;

// WebTransport session IDs are stream IDs.
using WebTransportSessionId = uint64_t;
// WebTransport stream reset codes are 32-bit.
using WebTransportStreamError = ::webtransport::StreamErrorCode;
// WebTransport session error codes are 32-bit.
using WebTransportSessionError = ::webtransport::SessionErrorCode;

enum : size_t { kQuicPathFrameBufferSize = 8 };
using QuicPathFrameBuffer = std::array<uint8_t, kQuicPathFrameBufferSize>;

// The connection id sequence number specifies the order that connection
// ids must be used in. This is also the sequence number carried in
// the IETF QUIC NEW_CONNECTION_ID and RETIRE_CONNECTION_ID frames.
using QuicConnectionIdSequenceNumber = uint64_t;

// A custom data that represents application-specific settings.
// In HTTP/3 for example, it includes the encoded SETTINGS.
using ApplicationState = std::vector<uint8_t>;

// A struct for functions which consume data payloads and fins.
struct QUICHE_EXPORT QuicConsumedData {
  constexpr QuicConsumedData(size_t bytes_consumed, bool fin_consumed)
      : bytes_consumed(bytes_consumed), fin_consumed(fin_consumed) {}

  // By default, gtest prints the raw bytes of an object. The bool data
  // member causes this object to have padding bytes, which causes the
  // default gtest object printer to read uninitialize memory. So we need
  // to teach gtest how to print this object.
  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const QuicConsumedData& s);

  // How many bytes were consumed.
  size_t bytes_consumed;

  // True if an incoming fin was consumed.
  bool fin_consumed;
};

// QuicAsyncStatus enumerates the possible results of an asynchronous
// operation.
enum QuicAsyncStatus {
  QUIC_SUCCESS = 0,
  QUIC_FAILURE = 1,
  // QUIC_PENDING results from an operation that will occur asynchronously. When
  // the operation is complete, a callback's |Run| method will be called.
  QUIC_PENDING = 2,
};

// TODO(wtc): see if WriteStatus can be replaced by QuicAsyncStatus.
enum WriteStatus : int16_t {
  WRITE_STATUS_OK,
  // Write is blocked, caller needs to retry.
  WRITE_STATUS_BLOCKED,
  // Write is blocked but the packet data is buffered, caller should not retry.
  WRITE_STATUS_BLOCKED_DATA_BUFFERED,
  // To make the IsWriteError(WriteStatus) function work properly:
  // - Non-errors MUST be added before WRITE_STATUS_ERROR.
  // - Errors MUST be added after WRITE_STATUS_ERROR.
  WRITE_STATUS_ERROR,
  WRITE_STATUS_MSG_TOO_BIG,
  WRITE_STATUS_FAILED_TO_COALESCE_PACKET,
  WRITE_STATUS_NUM_VALUES,
};

std::string HistogramEnumString(WriteStatus enum_value);
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const WriteStatus& status);

inline std::string HistogramEnumDescription(WriteStatus /*dummy*/) {
  return "status";
}

inline bool IsWriteBlockedStatus(WriteStatus status) {
  return status == WRITE_STATUS_BLOCKED ||
         status == WRITE_STATUS_BLOCKED_DATA_BUFFERED;
}

inline bool IsWriteError(WriteStatus status) {
  return status >= WRITE_STATUS_ERROR;
}

// A struct used to return the result of write calls including either the number
// of bytes written or the error code, depending upon the status.
struct QUICHE_EXPORT WriteResult {
  constexpr WriteResult(WriteStatus status, int bytes_written_or_error_code)
      : status(status), bytes_written(bytes_written_or_error_code) {}

  constexpr WriteResult() : WriteResult(WRITE_STATUS_ERROR, 0) {}

  bool operator==(const WriteResult& other) const {
    if (status != other.status) {
      return false;
    }
    switch (status) {
      case WRITE_STATUS_OK:
        return bytes_written == other.bytes_written;
      case WRITE_STATUS_BLOCKED:
      case WRITE_STATUS_BLOCKED_DATA_BUFFERED:
        return true;
      default:
        return error_code == other.error_code;
    }
  }

  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const WriteResult& s);

  WriteResult& set_batch_id(uint32_t new_batch_id) {
    batch_id = new_batch_id;
    return *this;
  }

  WriteStatus status;
  // Number of packets dropped as a result of this write.
  // Only used by batch writers. Otherwise always 0.
  uint16_t dropped_packets = 0;
  // The batch id the packet being written belongs to. For debugging only.
  // Only used by batch writers. Only valid if the packet being written started
  // a new batch, or added to an existing batch.
  uint32_t batch_id = 0;
  // The delta between a packet's ideal and actual send time:
  //     actual_send_time = ideal_send_time + send_time_offset
  //                      = (now + release_time_delay) + send_time_offset
  // Only valid if |status| is WRITE_STATUS_OK.
  QuicTime::Delta send_time_offset = QuicTime::Delta::Zero();
  // TODO(wub): In some cases, WRITE_STATUS_ERROR may set an error_code and
  // WRITE_STATUS_BLOCKED_DATA_BUFFERED may set bytes_written. This may need
  // some cleaning up so that perhaps both values can be set and valid.
  union {
    int bytes_written;  // only valid when status is WRITE_STATUS_OK
    int error_code;     // only valid when status is WRITE_STATUS_ERROR
  };
};

enum TransmissionType : int8_t {
  NOT_RETRANSMISSION,
  FIRST_TRANSMISSION_TYPE = NOT_RETRANSMISSION,
  HANDSHAKE_RETRANSMISSION,     // Retransmits due to handshake timeouts.
  ALL_ZERO_RTT_RETRANSMISSION,  // Retransmits all packets encrypted with 0-RTT
                                // key.
  LOSS_RETRANSMISSION,          // Retransmits due to loss detection.
  PTO_RETRANSMISSION,           // Retransmission due to probe timeout.
  PATH_RETRANSMISSION,          // Retransmission proactively due to underlying
                                // network change.
  ALL_INITIAL_RETRANSMISSION,   // Retransmit all packets encrypted with INITIAL
                                // key.
  LAST_TRANSMISSION_TYPE = ALL_INITIAL_RETRANSMISSION,
};

QUICHE_EXPORT std::string TransmissionTypeToString(
    TransmissionType transmission_type);

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       TransmissionType transmission_type);

enum HasRetransmittableData : uint8_t {
  NO_RETRANSMITTABLE_DATA,
  HAS_RETRANSMITTABLE_DATA,
};

enum IsHandshake : uint8_t { NOT_HANDSHAKE, IS_HANDSHAKE };

enum class Perspective : uint8_t { IS_SERVER, IS_CLIENT };

QUICHE_EXPORT std::string PerspectiveToString(Perspective perspective);
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const Perspective& perspective);

// Describes whether a ConnectionClose was originated by the peer.
enum class ConnectionCloseSource { FROM_PEER, FROM_SELF };

QUICHE_EXPORT std::string ConnectionCloseSourceToString(
    ConnectionCloseSource connection_close_source);
QUICHE_EXPORT std::ostream& operator<<(
    std::ostream& os, const ConnectionCloseSource& connection_close_source);

// Should a connection be closed silently or not.
enum class ConnectionCloseBehavior {
  SILENT_CLOSE,
  SILENT_CLOSE_WITH_CONNECTION_CLOSE_PACKET_SERIALIZED,
  SEND_CONNECTION_CLOSE_PACKET
};

QUICHE_EXPORT std::string ConnectionCloseBehaviorToString(
    ConnectionCloseBehavior connection_close_behavior);
QUICHE_EXPORT std::ostream& operator<<(
    std::ostream& os, const ConnectionCloseBehavior& connection_close_behavior);

enum QuicFrameType : uint8_t {
  // Regular frame types. The values set here cannot change without the
  // introduction of a new QUIC version.
  PADDING_FRAME = 0,
  RST_STREAM_FRAME = 1,
  CONNECTION_CLOSE_FRAME = 2,
  GOAWAY_FRAME = 3,
  WINDOW_UPDATE_FRAME = 4,
  BLOCKED_FRAME = 5,
  STOP_WAITING_FRAME = 6,
  PING_FRAME = 7,
  CRYPTO_FRAME = 8,
  // TODO(b/157935330): stop hard coding this when deprecate T050.
  HANDSHAKE_DONE_FRAME = 9,

  // STREAM and ACK frames are special frames. They are encoded differently on
  // the wire and their values do not need to be stable.
  STREAM_FRAME,
  ACK_FRAME,
  // The path MTU discovery frame is encoded as a PING frame on the wire.
  MTU_DISCOVERY_FRAME,

  // These are for IETF-specific frames for which there is no mapping
  // from Google QUIC frames. These are valid/allowed if and only if IETF-
  // QUIC has been negotiated. Values are not important, they are not
  // the values that are in the packets (see QuicIetfFrameType, below).
  NEW_CONNECTION_ID_FRAME,
  MAX_STREAMS_FRAME,
  STREAMS_BLOCKED_FRAME,
  PATH_RESPONSE_FRAME,
  PATH_CHALLENGE_FRAME,
  STOP_SENDING_FRAME,
  MESSAGE_FRAME,
  NEW_TOKEN_FRAME,
  RETIRE_CONNECTION_ID_FRAME,
  ACK_FREQUENCY_FRAME,
  RESET_STREAM_AT_FRAME,

  NUM_FRAME_TYPES
};

// Human-readable string suitable for logging.
QUICHE_EXPORT std::string QuicFrameTypeToString(QuicFrameType t);
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const QuicFrameType& t);

// Ietf frame types. These are defined in the IETF QUIC Specification.
// Explicit values are given in the enum so that we can be sure that
// the symbol will map to the correct stream type.
// All types are defined here, even if we have not yet implmented the
// quic/core/stream/.... stuff needed.
// Note: The protocol specifies that frame types are varint-62 encoded,
// further stating that the shortest encoding must be used.  The current set of
// frame types all have values less than 0x40 (64) so can be encoded in a single
// byte, with the two most significant bits being 0. Thus, the following
// enumerations are valid as both the numeric values of frame types AND their
// encodings.
enum QuicIetfFrameType : uint64_t {
  IETF_PADDING = 0x00,
  IETF_PING = 0x01,
  IETF_ACK = 0x02,
  IETF_ACK_ECN = 0x03,
  IETF_RST_STREAM = 0x04,
  IETF_STOP_SENDING = 0x05,
  IETF_CRYPTO = 0x06,
  IETF_NEW_TOKEN = 0x07,
  // the low-3 bits of the stream frame type value are actually flags
  // declaring what parts of the frame are/are-not present, as well as
  // some other control information. The code would then do something
  // along the lines of "if ((frame_type & 0xf8) == 0x08)" to determine
  // whether the frame is a stream frame or not, and then examine each
  // bit specifically when/as needed.
  IETF_STREAM = 0x08,
  // 0x09 through 0x0f are various flag settings of the IETF_STREAM frame.
  IETF_MAX_DATA = 0x10,
  IETF_MAX_STREAM_DATA = 0x11,
  IETF_MAX_STREAMS_BIDIRECTIONAL = 0x12,
  IETF_MAX_STREAMS_UNIDIRECTIONAL = 0x13,
  IETF_DATA_BLOCKED = 0x14,
  IETF_STREAM_DATA_BLOCKED = 0x15,
  IETF_STREAMS_BLOCKED_BIDIRECTIONAL = 0x16,
  IETF_STREAMS_BLOCKED_UNIDIRECTIONAL = 0x17,
  IETF_NEW_CONNECTION_ID = 0x18,
  IETF_RETIRE_CONNECTION_ID = 0x19,
  IETF_PATH_CHALLENGE = 0x1a,
  IETF_PATH_RESPONSE = 0x1b,
  // Both of the following are "Connection Close" frames,
  // the first signals transport-layer errors, the second application-layer
  // errors.
  IETF_CONNECTION_CLOSE = 0x1c,
  IETF_APPLICATION_CLOSE = 0x1d,

  IETF_HANDSHAKE_DONE = 0x1e,

  // The MESSAGE frame type has not yet been fully standardized.
  // QUIC versions starting with 46 and before 99 use 0x20-0x21.
  // IETF QUIC (v99) uses 0x30-0x31, see draft-pauly-quic-datagram.
  IETF_EXTENSION_MESSAGE_NO_LENGTH = 0x20,
  IETF_EXTENSION_MESSAGE = 0x21,
  IETF_EXTENSION_MESSAGE_NO_LENGTH_V99 = 0x30,
  IETF_EXTENSION_MESSAGE_V99 = 0x31,

  // An QUIC extension frame for sender control of acknowledgement delays
  IETF_ACK_FREQUENCY = 0xaf,

  // A QUIC extension frame which augments the IETF_ACK frame definition with
  // packet receive timestamps.
  // TODO(ianswett): Determine a proper value to replace this temporary value.
  IETF_ACK_RECEIVE_TIMESTAMPS = 0x22,

  // https://datatracker.ietf.org/doc/html/draft-ietf-quic-reliable-stream-reset
  IETF_RESET_STREAM_AT = 0x24,
};
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const QuicIetfFrameType& c);
QUICHE_EXPORT std::string QuicIetfFrameTypeString(QuicIetfFrameType t);

// Masks for the bits that indicate the frame is a Stream frame vs the
// bits used as flags.
#define IETF_STREAM_FRAME_TYPE_MASK 0xfffffffffffffff8
#define IETF_STREAM_FRAME_FLAG_MASK 0x07
#define IS_IETF_STREAM_FRAME(_stype_) \
  (((_stype_) & IETF_STREAM_FRAME_TYPE_MASK) == IETF_STREAM)

// These are the values encoded in the low-order 3 bits of the
// IETF_STREAMx frame type.
#define IETF_STREAM_FRAME_FIN_BIT 0x01
#define IETF_STREAM_FRAME_LEN_BIT 0x02
#define IETF_STREAM_FRAME_OFF_BIT 0x04

enum QuicPacketNumberLength : uint8_t {
  PACKET_1BYTE_PACKET_NUMBER = 1,
  PACKET_2BYTE_PACKET_NUMBER = 2,
  PACKET_3BYTE_PACKET_NUMBER = 3,  // Used in versions 45+.
  PACKET_4BYTE_PACKET_NUMBER = 4,
  IETF_MAX_PACKET_NUMBER_LENGTH = 4,
  // TODO(b/145819870): Remove 6 and 8 when we remove Q043 since these values
  // are not representable with later versions.
  PACKET_6BYTE_PACKET_NUMBER = 6,
  PACKET_8BYTE_PACKET_NUMBER = 8
};

// Used to indicate a QuicSequenceNumberLength using two flag bits.
enum QuicPacketNumberLengthFlags {
  PACKET_FLAGS_1BYTE_PACKET = 0,           // 00
  PACKET_FLAGS_2BYTE_PACKET = 1,           // 01
  PACKET_FLAGS_4BYTE_PACKET = 1 << 1,      // 10
  PACKET_FLAGS_8BYTE_PACKET = 1 << 1 | 1,  // 11
};

// The public flags are specified in one byte.
enum QuicPacketPublicFlags {
  PACKET_PUBLIC_FLAGS_NONE = 0,

  // Bit 0: Does the packet header contains version info?
  PACKET_PUBLIC_FLAGS_VERSION = 1 << 0,

  // Bit 1: Is this packet a public reset packet?
  PACKET_PUBLIC_FLAGS_RST = 1 << 1,

  // Bit 2: indicates the header includes a nonce.
  PACKET_PUBLIC_FLAGS_NONCE = 1 << 2,

  // Bit 3: indicates whether a ConnectionID is included.
  PACKET_PUBLIC_FLAGS_0BYTE_CONNECTION_ID = 0,
  PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID = 1 << 3,

  // Deprecated version 32 and earlier used two bits to indicate an 8-byte
  // connection ID. We send this from the client because of some broken
  // middleboxes that are still checking this bit.
  PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID_OLD = 1 << 3 | 1 << 2,

  // Bits 4 and 5 describe the packet number length as follows:
  // --00----: 1 byte
  // --01----: 2 bytes
  // --10----: 4 bytes
  // --11----: 6 bytes
  PACKET_PUBLIC_FLAGS_1BYTE_PACKET = PACKET_FLAGS_1BYTE_PACKET << 4,
  PACKET_PUBLIC_FLAGS_2BYTE_PACKET = PACKET_FLAGS_2BYTE_PACKET << 4,
  PACKET_PUBLIC_FLAGS_4BYTE_PACKET = PACKET_FLAGS_4BYTE_PACKET << 4,
  PACKET_PUBLIC_FLAGS_6BYTE_PACKET = PACKET_FLAGS_8BYTE_PACKET << 4,

  // Reserved, unimplemented flags:

  // Bit 7: indicates the presence of a second flags byte.
  PACKET_PUBLIC_FLAGS_TWO_OR_MORE_BYTES = 1 << 7,

  // All bits set (bits 6 and 7 are not currently used): 00111111
  PACKET_PUBLIC_FLAGS_MAX = (1 << 6) - 1,
};

// The private flags are specified in one byte.
enum QuicPacketPrivateFlags {
  PACKET_PRIVATE_FLAGS_NONE = 0,

  // Bit 0: Does this packet contain an entropy bit?
  PACKET_PRIVATE_FLAGS_ENTROPY = 1 << 0,

  // (bits 1-7 are not used): 00000001
  PACKET_PRIVATE_FLAGS_MAX = (1 << 1) - 1
};

// Defines for all types of congestion control algorithms that can be used in
// QUIC. Note that this is separate from the congestion feedback type -
// some congestion control algorithms may use the same feedback type
// (Reno and Cubic are the classic example for that).
enum CongestionControlType {
  kCubicBytes,
  kRenoBytes,
  kBBR,
  kPCC,
  kGoogCC,
  kBBRv2,  // TODO(rch): This is effectively BBRv3. We should finish the
           // implementation and rename this enum.
  kPragueCubic,
};

QUICHE_EXPORT std::string CongestionControlTypeToString(
    CongestionControlType cc_type);

// EncryptionLevel enumerates the stages of encryption that a QUIC connection
// progresses through. When retransmitting a packet, the encryption level needs
// to be specified so that it is retransmitted at a level which the peer can
// understand.
enum EncryptionLevel : int8_t {
  ENCRYPTION_INITIAL = 0,
  ENCRYPTION_HANDSHAKE = 1,
  ENCRYPTION_ZERO_RTT = 2,
  ENCRYPTION_FORWARD_SECURE = 3,

  NUM_ENCRYPTION_LEVELS,
};

inline bool EncryptionLevelIsValid(EncryptionLevel level) {
  return ENCRYPTION_INITIAL <= level && level < NUM_ENCRYPTION_LEVELS;
}

QUICHE_EXPORT std::string EncryptionLevelToString(EncryptionLevel level);

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os, EncryptionLevel level);

// Enumeration of whether a server endpoint will request a client certificate,
// and whether that endpoint requires a valid client certificate to establish a
// connection.
enum class ClientCertMode : uint8_t {
  kNone,     // Do not request a client certificate.  Default server behavior.
  kRequest,  // Request a certificate, but allow unauthenticated connections.
  kRequire,  // Require clients to provide a valid certificate.
};

QUICHE_EXPORT absl::string_view ClientCertModeToString(ClientCertMode mode);

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os, ClientCertMode mode);

enum AddressChangeType : uint8_t {
  // IP address and port remain unchanged.
  NO_CHANGE,
  // Port changed, but IP address remains unchanged.
  PORT_CHANGE,
  // IPv4 address changed, but within the /24 subnet (port may have changed.)
  IPV4_SUBNET_CHANGE,
  // IPv4 address changed, excluding /24 subnet change (port may have changed.)
  IPV4_TO_IPV4_CHANGE,
  // IP address change from an IPv4 to an IPv6 address (port may have changed.)
  IPV4_TO_IPV6_CHANGE,
  // IP address change from an IPv6 to an IPv4 address (port may have changed.)
  IPV6_TO_IPV4_CHANGE,
  // IP address change from an IPv6 to an IPv6 address (port may have changed.)
  IPV6_TO_IPV6_CHANGE,
};

QUICHE_EXPORT std::string AddressChangeTypeToString(AddressChangeType type);

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       AddressChangeType type);

enum StreamSendingState {
  // Sender has more data to send on this stream.
  NO_FIN,
  // Sender is done sending on this stream.
  FIN,
  // Sender is done sending on this stream and random padding needs to be
  // appended after all stream frames.
  FIN_AND_PADDING,
};

enum SentPacketState : uint8_t {
  // The packet is in flight and waiting to be acked.
  OUTSTANDING,
  FIRST_PACKET_STATE = OUTSTANDING,
  // The packet was never sent.
  NEVER_SENT,
  // The packet has been acked.
  ACKED,
  // This packet is not expected to be acked.
  UNACKABLE,
  // This packet has been delivered or unneeded.
  NEUTERED,

  // States below are corresponding to retransmission types in TransmissionType.

  // This packet has been retransmitted when retransmission timer fires in
  // HANDSHAKE mode.
  HANDSHAKE_RETRANSMITTED,
  // This packet is considered as lost, this is used for LOST_RETRANSMISSION.
  LOST,
  // This packet has been retransmitted when PTO fires.
  PTO_RETRANSMITTED,
  // This packet is sent on a different path or is a PING only packet.
  // Do not update RTT stats and congestion control if the packet is the
  // largest_acked of an incoming ACK.
  NOT_CONTRIBUTING_RTT,
  LAST_PACKET_STATE = NOT_CONTRIBUTING_RTT,
};

enum PacketHeaderFormat : uint8_t {
  IETF_QUIC_LONG_HEADER_PACKET,
  IETF_QUIC_SHORT_HEADER_PACKET,
  GOOGLE_QUIC_PACKET,
};

QUICHE_EXPORT std::string PacketHeaderFormatToString(PacketHeaderFormat format);

// Information about a newly acknowledged packet.
struct QUICHE_EXPORT AckedPacket {
  constexpr AckedPacket(QuicPacketNumber packet_number,
                        QuicPacketLength bytes_acked,
                        QuicTime receive_timestamp)
      : packet_number(packet_number),
        bytes_acked(bytes_acked),
        receive_timestamp(receive_timestamp) {}

  friend QUICHE_EXPORT std::ostream& operator<<(
      std::ostream& os, const AckedPacket& acked_packet);

  QuicPacketNumber packet_number;
  // Number of bytes sent in the packet that was acknowledged.
  QuicPacketLength bytes_acked;
  // Whether the packet has been marked as lost before the ack. |bytes_acked|
  // should be 0 if this is true.
  bool spurious_loss = false;
  // The time |packet_number| was received by the peer, according to the
  // optional timestamp the peer included in the ACK frame which acknowledged
  // |packet_number|. Zero if no timestamp was available for this packet.
  QuicTime receive_timestamp;
};

// A vector of acked packets.
using AckedPacketVector = absl::InlinedVector<AckedPacket, 2>;

// Information about a newly lost packet.
struct QUICHE_EXPORT LostPacket {
  LostPacket(QuicPacketNumber packet_number, QuicPacketLength bytes_lost)
      : packet_number(packet_number), bytes_lost(bytes_lost) {}

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const LostPacket& lost_packet);

  QuicPacketNumber packet_number;
  // Number of bytes sent in the packet that was lost.
  QuicPacketLength bytes_lost;
};

// A vector of lost packets.
using LostPacketVector = absl::InlinedVector<LostPacket, 2>;

// Please note, this value cannot used directly for packet serialization.
enum QuicLongHeaderType : uint8_t {
  VERSION_NEGOTIATION,
  INITIAL,
  ZERO_RTT_PROTECTED,
  HANDSHAKE,
  RETRY,

  INVALID_PACKET_TYPE,
};

QUICHE_EXPORT std::string QuicLongHeaderTypeToString(QuicLongHeaderType type);

enum QuicPacketHeaderTypeFlags : uint8_t {
  // Bit 2: Key phase bit for IETF QUIC short header packets.
  FLAGS_KEY_PHASE_BIT = 1 << 2,
  // Bit 3: Google QUIC Demultiplexing bit, the short header always sets this
  // bit to 0, allowing to distinguish Google QUIC packets from short header
  // packets.
  FLAGS_DEMULTIPLEXING_BIT = 1 << 3,
  // Bits 4 and 5: Reserved bits for short header.
  FLAGS_SHORT_HEADER_RESERVED_1 = 1 << 4,
  FLAGS_SHORT_HEADER_RESERVED_2 = 1 << 5,
  // Bit 6: the 'QUIC' bit.
  FLAGS_FIXED_BIT = 1 << 6,
  // Bit 7: Indicates the header is long or short header.
  FLAGS_LONG_HEADER = 1 << 7,
};

enum MessageStatus {
  MESSAGE_STATUS_SUCCESS,
  MESSAGE_STATUS_ENCRYPTION_NOT_ESTABLISHED,  // Failed to send message because
                                              // encryption is not established
                                              // yet.
  MESSAGE_STATUS_UNSUPPORTED,  // Failed to send message because MESSAGE frame
                               // is not supported by the connection.
  MESSAGE_STATUS_BLOCKED,      // Failed to send message because connection is
                           // congestion control blocked or underlying socket is
                           // write blocked.
  MESSAGE_STATUS_TOO_LARGE,  // Failed to send message because the message is
                             // too large to fit into a single packet.
  MESSAGE_STATUS_SETTINGS_NOT_RECEIVED,  // Failed to send message because
                                         // SETTINGS frame has not been received
                                         // yet.
  MESSAGE_STATUS_INTERNAL_ERROR,  // Failed to send message because connection
                                  // reaches an invalid state.
};

QUICHE_EXPORT std::string MessageStatusToString(MessageStatus message_status);

// Used to return the result of SendMessage calls
struct QUICHE_EXPORT MessageResult {
  MessageResult(MessageStatus status, QuicMessageId message_id);

  bool operator==(const MessageResult& other) const {
    return status == other.status && message_id == other.message_id;
  }

  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const MessageResult& mr);

  MessageStatus status;
  // Only valid when status is MESSAGE_STATUS_SUCCESS.
  QuicMessageId message_id;
};

QUICHE_EXPORT std::string MessageResultToString(MessageResult message_result);

enum WriteStreamDataResult {
  WRITE_SUCCESS,
  STREAM_MISSING,  // Trying to write data of a nonexistent stream (e.g.
                   // closed).
  WRITE_FAILED,    // Trying to write nonexistent data of a stream
};

enum StreamType : uint8_t {
  // Bidirectional streams allow for data to be sent in both directions.
  BIDIRECTIONAL,

  // Unidirectional streams carry data in one direction only.
  WRITE_UNIDIRECTIONAL,
  READ_UNIDIRECTIONAL,
  // Not actually a stream type. Used only by QuicCryptoStream when it uses
  // CRYPTO frames and isn't actually a QuicStream.
  CRYPTO,
};

// A packet number space is the context in which a packet can be processed and
// acknowledged.
enum PacketNumberSpace : uint8_t {
  INITIAL_DATA = 0,  // Only used in IETF QUIC.
  HANDSHAKE_DATA = 1,
  APPLICATION_DATA = 2,

  NUM_PACKET_NUMBER_SPACES,
};

QUICHE_EXPORT std::string PacketNumberSpaceToString(
    PacketNumberSpace packet_number_space);

// Used to return the result of processing a received ACK frame.
enum AckResult {
  PACKETS_NEWLY_ACKED,
  NO_PACKETS_NEWLY_ACKED,
  UNSENT_PACKETS_ACKED,     // Peer acks unsent packets.
  UNACKABLE_PACKETS_ACKED,  // Peer acks packets that are not expected to be
                            // acked. For example, encryption is reestablished,
                            // and all sent encrypted packets cannot be
                            // decrypted by the peer. Version gets negotiated,
                            // and all sent packets in the different version
                            // cannot be processed by the peer.
  PACKETS_ACKED_IN_WRONG_PACKET_NUMBER_SPACE,
};

// Used to return the result of processing a received NEW_CID frame.
enum class NewConnectionIdResult : uint8_t {
  kOk,
  kDuplicateFrame,  // Not an error.
  kProtocolViolation,
};

// Indicates the fate of a serialized packet in WritePacket().
enum SerializedPacketFate : uint8_t {
  DISCARD,         // Discard the packet.
  COALESCE,        // Try to coalesce packet.
  BUFFER,          // Buffer packet in buffered_packets_.
  SEND_TO_WRITER,  // Send packet to writer.
};

QUICHE_EXPORT std::string SerializedPacketFateToString(
    SerializedPacketFate fate);

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const SerializedPacketFate fate);

// There are three different forms of CONNECTION_CLOSE.
enum QuicConnectionCloseType {
  GOOGLE_QUIC_CONNECTION_CLOSE = 0,
  IETF_QUIC_TRANSPORT_CONNECTION_CLOSE = 1,
  IETF_QUIC_APPLICATION_CONNECTION_CLOSE = 2
};

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const QuicConnectionCloseType type);

QUICHE_EXPORT std::string QuicConnectionCloseTypeString(
    QuicConnectionCloseType type);

// Indicate handshake state of a connection.
enum HandshakeState {
  // Initial state.
  HANDSHAKE_START,
  // Only used in IETF QUIC with TLS handshake. State proceeds to
  // HANDSHAKE_PROCESSED after a packet of HANDSHAKE packet number space
  // gets successfully processed, and the initial key can be dropped.
  HANDSHAKE_PROCESSED,
  // In QUIC crypto, state proceeds to HANDSHAKE_COMPLETE if client receives
  // SHLO or server successfully processes an ENCRYPTION_FORWARD_SECURE
  // packet, such that the handshake packets can be neutered. In IETF QUIC
  // with TLS handshake, state proceeds to HANDSHAKE_COMPLETE once the client
  // has both 1-RTT send and receive keys.
  HANDSHAKE_COMPLETE,
  // Only used in IETF QUIC with TLS handshake. State proceeds to
  // HANDSHAKE_CONFIRMED if 1) a client receives HANDSHAKE_DONE frame or
  // acknowledgment for 1-RTT packet or 2) server has
  // 1-RTT send and receive keys.
  HANDSHAKE_CONFIRMED,
};

struct QUICHE_EXPORT NextReleaseTimeResult {
  // The ideal release time of the packet being sent.
  QuicTime release_time;
  // Whether it is allowed to send the packet before release_time.
  bool allow_burst;
};

// QuicPacketBuffer bundles a buffer and a function that releases it. Note
// it does not assume ownership of buffer, i.e. it doesn't release the buffer on
// destruction.
struct QUICHE_EXPORT QuicPacketBuffer {
  QuicPacketBuffer() = default;

  QuicPacketBuffer(char* buffer,
                   std::function<void(const char*)> release_buffer)
      : buffer(buffer), release_buffer(std::move(release_buffer)) {}

  char* buffer = nullptr;
  std::function<void(const char*)> release_buffer;
};

// QuicOwnedPacketBuffer is a QuicPacketBuffer that assumes buffer ownership.
struct QUICHE_EXPORT QuicOwnedPacketBuffer : public QuicPacketBuffer {
  QuicOwnedPacketBuffer(const QuicOwnedPacketBuffer&) = delete;
  QuicOwnedPacketBuffer& operator=(const QuicOwnedPacketBuffer&) = delete;

  QuicOwnedPacketBuffer(char* buffer,
                        std::function<void(const char*)> release_buffer)
      : QuicPacketBuffer(buffer, std::move(release_buffer)) {}

  QuicOwnedPacketBuffer(QuicOwnedPacketBuffer&& owned_buffer)
      : QuicPacketBuffer(std::move(owned_buffer)) {
    // |owned_buffer| does not own a buffer any more.
    owned_buffer.buffer = nullptr;
  }

  explicit QuicOwnedPacketBuffer(QuicPacketBuffer&& packet_buffer)
      : QuicPacketBuffer(std::move(packet_buffer)) {}

  ~QuicOwnedPacketBuffer() {
    if (release_buffer != nullptr && buffer != nullptr) {
      release_buffer(buffer);
    }
  }
};

// These values must remain stable as they are uploaded to UMA histograms.
enum class KeyUpdateReason {
  kInvalid = 0,
  kRemote = 1,
  kLocalForTests = 2,
  kLocalForInteropRunner = 3,
  kLocalAeadConfidentialityLimit = 4,
  kLocalKeyUpdateLimitOverride = 5,
  kMaxValue = kLocalKeyUpdateLimitOverride,
};

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const KeyUpdateReason reason);

QUICHE_EXPORT std::string KeyUpdateReasonString(KeyUpdateReason reason);

using QuicSignatureAlgorithmVector = absl::InlinedVector<uint16_t, 8>;

// QuicSSLConfig contains configurations to be applied on a SSL object, which
// overrides the configurations in SSL_CTX.
struct QUICHE_EXPORT QuicSSLConfig {
  // Whether TLS early data should be enabled. If not set, default to enabled.
  std::optional<bool> early_data_enabled;
  // Whether TLS session tickets are supported. If not set, default to
  // supported.
  std::optional<bool> disable_ticket_support;
  // If set, used to configure the SSL object with
  // SSL_set_signing_algorithm_prefs.
  std::optional<QuicSignatureAlgorithmVector> signing_algorithm_prefs;
  // Client certificate mode for mTLS support. Only used at server side.
  ClientCertMode client_cert_mode = ClientCertMode::kNone;
  // As a client, the ECHConfigList to use with ECH. If empty, ECH is not
  // offered.
  std::string ech_config_list;
  // As a client, whether ECH GREASE is enabled. If `ech_config_list` is
  // not empty, this value does nothing.
  bool ech_grease_enabled = false;
};

QUICHE_EXPORT bool operator==(const QuicSSLConfig& lhs,
                              const QuicSSLConfig& rhs);

// QuicDelayedSSLConfig contains a subset of SSL config that can be applied
// after BoringSSL's early select certificate callback. This overwrites all SSL
// configs applied before cert selection.
struct QUICHE_EXPORT QuicDelayedSSLConfig {
  // Client certificate mode for mTLS support. Only used at server side.
  // std::nullopt means do not change client certificate mode.
  std::optional<ClientCertMode> client_cert_mode;
  // QUIC transport parameters as serialized by ProofSourceHandle.
  std::optional<std::vector<uint8_t>> quic_transport_parameters;
};

// ParsedClientHello contains client hello information extracted from a fully
// received client hello.
struct QUICHE_EXPORT ParsedClientHello {
  std::string sni;                               // QUIC crypto and TLS.
  std::string uaid;                              // QUIC crypto only.
  std::vector<uint16_t> supported_groups;        // TLS only.
  std::vector<uint16_t> cert_compression_algos;  // TLS only.
  std::vector<std::string> alpns;                // QUIC crypto and TLS.
  // The unvalidated retry token from the last received packet of a potentially
  // multi-packet client hello. TLS only.
  std::string retry_token;
  bool resumption_attempted = false;  // TLS only.
  bool early_data_attempted = false;  // TLS only.

  std::string ToString() const;
};

QUICHE_EXPORT bool operator==(const ParsedClientHello& a,
                              const ParsedClientHello& b);

QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                       const ParsedClientHello& parsed_chlo);

// The two bits in the IP header for Explicit Congestion Notification can take
// one of four values.
enum QuicEcnCodepoint {
  // The NOT-ECT codepoint, indicating the packet sender is not using (or the
  // network has disabled) ECN.
  ECN_NOT_ECT = 0,
  // The ECT(1) codepoint, indicating the packet sender is using Low Latency,
  // Low Loss, Scalable Throughput (L4S) ECN (RFC9330).
  ECN_ECT1 = 1,
  // The ECT(0) codepoint, indicating the packet sender is using classic ECN
  // (RFC3168).
  ECN_ECT0 = 2,
  // The CE ("Congestion Experienced") codepoint, indicating the packet sender
  // is using ECN, and a router is experiencing congestion.
  ECN_CE = 3,
};

QUICHE_EXPORT std::string EcnCodepointToString(QuicEcnCodepoint ecn);

// This struct reports the Explicit Congestion Notification (ECN) contents of
// the ACK_ECN frame. They are the cumulative number of QUIC packets received
// for that codepoint in a given Packet Number Space.
struct QUICHE_EXPORT QuicEcnCounts {
  QuicEcnCounts() = default;
  QuicEcnCounts(QuicPacketCount ect0, QuicPacketCount ect1, QuicPacketCount ce)
      : ect0(ect0), ect1(ect1), ce(ce) {}

  std::string ToString() const {
    return absl::StrFormat("ECT(0): %s, ECT(1): %s, CE: %s",
                           std::to_string(ect0), std::to_string(ect1),
                           std::to_string(ce));
  }

  bool operator==(const QuicEcnCounts& other) const {
    return (this->ect0 == other.ect0 && this->ect1 == other.ect1 &&
            this->ce == other.ce);
  }

  QuicPacketCount ect0 = 0;
  QuicPacketCount ect1 = 0;
  QuicPacketCount ce = 0;
};

// Type of the priorities used by a QUIC session.
enum class QuicPriorityType : uint8_t {
  // HTTP priorities as defined by RFC 9218
  kHttp,
  // WebTransport priorities as defined by <https://w3c.github.io/webtransport/>
  kWebTransport,
};

QUICHE_EXPORT std::string QuicPriorityTypeToString(QuicPriorityType type);
QUICHE_EXPORT std::ostream& operator<<(std::ostream& os, QuicPriorityType type);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_TYPES_H_
