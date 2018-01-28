// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_TYPES_H_
#define NET_QUIC_CORE_QUIC_TYPES_H_

#include <array>
#include <cstddef>
#include <map>
#include <ostream>
#include <vector>

#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

typedef uint16_t QuicPacketLength;
typedef uint32_t QuicHeaderId;
typedef uint32_t QuicStreamId;
typedef uint64_t QuicByteCount;
typedef uint64_t QuicConnectionId;
typedef uint64_t QuicPacketCount;
typedef uint64_t QuicPacketNumber;
typedef uint64_t QuicPublicResetNonceProof;
typedef uint64_t QuicStreamOffset;
typedef std::array<char, 32> DiversificationNonce;
typedef std::vector<std::pair<QuicPacketNumber, QuicTime>> PacketTimeVector;

// A struct for functions which consume data payloads and fins.
struct QUIC_EXPORT_PRIVATE QuicConsumedData {
  QuicConsumedData(size_t bytes_consumed, bool fin_consumed);

  // By default, gtest prints the raw bytes of an object. The bool data
  // member causes this object to have padding bytes, which causes the
  // default gtest object printer to read uninitialize memory. So we need
  // to teach gtest how to print this object.
  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
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
  // QUIC_PENDING results from an operation that will occur asynchonously. When
  // the operation is complete, a callback's |Run| method will be called.
  QUIC_PENDING = 2,
};

// TODO(wtc): see if WriteStatus can be replaced by QuicAsyncStatus.
enum WriteStatus {
  WRITE_STATUS_OK,
  WRITE_STATUS_BLOCKED,
  WRITE_STATUS_ERROR,
};

// A struct used to return the result of write calls including either the number
// of bytes written or the error code, depending upon the status.
struct QUIC_EXPORT_PRIVATE WriteResult {
  WriteResult(WriteStatus status, int bytes_written_or_error_code);
  WriteResult();

  WriteStatus status;
  union {
    int bytes_written;  // only valid when status is WRITE_STATUS_OK
    int error_code;     // only valid when status is WRITE_STATUS_ERROR
  };
};

enum TransmissionType : int8_t {
  NOT_RETRANSMISSION,
  FIRST_TRANSMISSION_TYPE = NOT_RETRANSMISSION,
  HANDSHAKE_RETRANSMISSION,    // Retransmits due to handshake timeouts.
  ALL_UNACKED_RETRANSMISSION,  // Retransmits all unacked packets.
  ALL_INITIAL_RETRANSMISSION,  // Retransmits all initially encrypted packets.
  LOSS_RETRANSMISSION,         // Retransmits due to loss detection.
  RTO_RETRANSMISSION,          // Retransmits due to retransmit time out.
  TLP_RETRANSMISSION,          // Tail loss probes.
  LAST_TRANSMISSION_TYPE = TLP_RETRANSMISSION,
};

enum HasRetransmittableData : int8_t {
  NO_RETRANSMITTABLE_DATA,
  HAS_RETRANSMITTABLE_DATA,
};

enum IsHandshake : int8_t { NOT_HANDSHAKE, IS_HANDSHAKE };

enum class Perspective { IS_SERVER, IS_CLIENT };
QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                             const Perspective& s);

// Describes whether a ConnectionClose was originated by the peer.
enum class ConnectionCloseSource { FROM_PEER, FROM_SELF };

// Should a connection be closed silently or not.
enum class ConnectionCloseBehavior {
  SILENT_CLOSE,
  SEND_CONNECTION_CLOSE_PACKET,
  SEND_CONNECTION_CLOSE_PACKET_WITH_NO_ACK
};

enum QuicFrameType {
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

  // STREAM and ACK frames are special frames. They are encoded differently on
  // the wire and their values do not need to be stable.
  STREAM_FRAME,
  ACK_FRAME,
  // The path MTU discovery frame is encoded as a PING frame on the wire.
  MTU_DISCOVERY_FRAME,
  NUM_FRAME_TYPES
};

enum QuicConnectionIdLength {
  PACKET_0BYTE_CONNECTION_ID = 0,
  PACKET_8BYTE_CONNECTION_ID = 8
};

enum QuicPacketNumberLength : int8_t {
  PACKET_1BYTE_PACKET_NUMBER = 1,
  PACKET_2BYTE_PACKET_NUMBER = 2,
  PACKET_4BYTE_PACKET_NUMBER = 4,
  // TODO(rch): Remove this when we remove QUIC_VERSION_39.
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

  // Bit 2: indicates the that public header includes a nonce.
  PACKET_PUBLIC_FLAGS_NONCE = 1 << 2,

  // Bit 3: indicates whether a ConnectionID is included.
  PACKET_PUBLIC_FLAGS_0BYTE_CONNECTION_ID = 0,
  PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID = 1 << 3,

  // QUIC_VERSION_32 and earlier use two bits for an 8 byte
  // connection id.
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
  kCubic,
  kCubicBytes,
  kReno,
  kRenoBytes,
  kBBR,
  kPCC
};

enum LossDetectionType {
  kNack,          // Used to mimic TCP's loss detection.
  kTime,          // Time based loss detection.
  kAdaptiveTime,  // Adaptive time based loss detection.
  kLazyFack,      // Nack based but with FACK disabled for the first ack.
};

// EncryptionLevel enumerates the stages of encryption that a QUIC connection
// progresses through. When retransmitting a packet, the encryption level needs
// to be specified so that it is retransmitted at a level which the peer can
// understand.
enum EncryptionLevel : int8_t {
  ENCRYPTION_NONE = 0,
  ENCRYPTION_INITIAL = 1,
  ENCRYPTION_FORWARD_SECURE = 2,

  NUM_ENCRYPTION_LEVELS,
};

enum PeerAddressChangeType {
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

enum StreamSendingState {
  // Sender has more data to send on this stream.
  NO_FIN,
  // Sender is done sending on this stream.
  FIN,
  // Sender is done sending on this stream and random padding needs to be
  // appended after all stream frames.
  FIN_AND_PADDING,
};

// Information about a newly acknowledged packet.
struct AckedPacket {
  AckedPacket(QuicPacketNumber packet_number,
              QuicPacketLength bytes_acked,
              QuicTime receive_timestamp)
      : packet_number(packet_number),
        bytes_acked(bytes_acked),
        receive_timestamp(receive_timestamp) {}

  QuicPacketNumber packet_number;
  // Number of bytes sent in the packet that was acknowledged.
  QuicPacketLength bytes_acked;
  // The time |packet_number| was received by the peer, according to the
  // optional timestamp the peer included in the ACK frame which acknowledged
  // |packet_number|. Zero if no timestamp was available for this packet.
  QuicTime receive_timestamp;
};

// A vector of acked packets.
typedef std::vector<AckedPacket> AckedPacketVector;

// Information about a newly lost packet.
struct LostPacket {
  LostPacket(QuicPacketNumber packet_number, QuicPacketLength bytes_lost)
      : packet_number(packet_number), bytes_lost(bytes_lost) {}

  QuicPacketNumber packet_number;
  // Number of bytes sent in the packet that was lost.
  QuicPacketLength bytes_lost;
};

// A vector of lost packets.
typedef std::vector<LostPacket> LostPacketVector;

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_TYPES_H_
