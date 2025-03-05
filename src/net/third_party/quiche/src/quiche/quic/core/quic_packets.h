// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PACKETS_H_
#define QUICHE_QUIC_CORE_QUIC_PACKETS_H_

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/quic_ack_listener_interface.h"
#include "quiche/quic/core/quic_bandwidth.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_time.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"

namespace quic {

class QuicPacket;
struct QuicPacketHeader;

// Returns the destination connection ID of |header| when |perspective| is
// server, and the source connection ID when |perspective| is client.
QUICHE_EXPORT QuicConnectionId GetServerConnectionIdAsRecipient(
    const QuicPacketHeader& header, Perspective perspective);

// Returns the destination connection ID of |header| when |perspective| is
// client, and the source connection ID when |perspective| is server.
QUICHE_EXPORT QuicConnectionId GetClientConnectionIdAsRecipient(
    const QuicPacketHeader& header, Perspective perspective);

// Returns the destination connection ID of |header| when |perspective| is
// client, and the source connection ID when |perspective| is server.
QUICHE_EXPORT QuicConnectionId GetServerConnectionIdAsSender(
    const QuicPacketHeader& header, Perspective perspective);

// Returns the destination connection ID included of |header| when |perspective|
// is client, and the source connection ID included when |perspective| is
// server.
QUICHE_EXPORT QuicConnectionIdIncluded GetServerConnectionIdIncludedAsSender(
    const QuicPacketHeader& header, Perspective perspective);

// Returns the destination connection ID of |header| when |perspective| is
// server, and the source connection ID when |perspective| is client.
QUICHE_EXPORT QuicConnectionId GetClientConnectionIdAsSender(
    const QuicPacketHeader& header, Perspective perspective);

// Returns the destination connection ID included of |header| when |perspective|
// is server, and the source connection ID included when |perspective| is
// client.
QUICHE_EXPORT QuicConnectionIdIncluded GetClientConnectionIdIncludedAsSender(
    const QuicPacketHeader& header, Perspective perspective);

// Number of connection ID bytes that are actually included over the wire.
QUICHE_EXPORT uint8_t
GetIncludedConnectionIdLength(QuicConnectionId connection_id,
                              QuicConnectionIdIncluded connection_id_included);

// Number of destination connection ID bytes that are actually included over the
// wire for this particular header.
QUICHE_EXPORT uint8_t
GetIncludedDestinationConnectionIdLength(const QuicPacketHeader& header);

// Number of source connection ID bytes that are actually included over the
// wire for this particular header.
QUICHE_EXPORT uint8_t
GetIncludedSourceConnectionIdLength(const QuicPacketHeader& header);

// Size in bytes of the data packet header.
QUICHE_EXPORT size_t GetPacketHeaderSize(QuicTransportVersion version,
                                         const QuicPacketHeader& header);

QUICHE_EXPORT size_t GetPacketHeaderSize(
    QuicTransportVersion version, uint8_t destination_connection_id_length,
    uint8_t source_connection_id_length, bool include_version,
    bool include_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    quiche::QuicheVariableLengthIntegerLength retry_token_length_length,
    QuicByteCount retry_token_length,
    quiche::QuicheVariableLengthIntegerLength length_length);

// Index of the first byte in a QUIC packet of encrypted data.
QUICHE_EXPORT size_t GetStartOfEncryptedData(QuicTransportVersion version,
                                             const QuicPacketHeader& header);

QUICHE_EXPORT size_t GetStartOfEncryptedData(
    QuicTransportVersion version, uint8_t destination_connection_id_length,
    uint8_t source_connection_id_length, bool include_version,
    bool include_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    quiche::QuicheVariableLengthIntegerLength retry_token_length_length,
    QuicByteCount retry_token_length,
    quiche::QuicheVariableLengthIntegerLength length_length);

struct QUICHE_EXPORT QuicPacketHeader {
  QuicPacketHeader();
  QuicPacketHeader(const QuicPacketHeader& other);
  ~QuicPacketHeader();

  QuicPacketHeader& operator=(const QuicPacketHeader& other);

  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const QuicPacketHeader& header);

  // Universal header. All QuicPacket headers will have a connection_id and
  // public flags.
  QuicConnectionId destination_connection_id;
  QuicConnectionIdIncluded destination_connection_id_included;
  QuicConnectionId source_connection_id;
  QuicConnectionIdIncluded source_connection_id_included;
  // This is only used for Google QUIC.
  bool reset_flag;
  // For Google QUIC, version flag in packets from the server means version
  // negotiation packet. For IETF QUIC, version flag means long header.
  bool version_flag;
  // Indicates whether |possible_stateless_reset_token| contains a valid value
  // parsed from the packet buffer. IETF QUIC only, always false for GQUIC.
  bool has_possible_stateless_reset_token;
  QuicPacketNumberLength packet_number_length;
  uint8_t type_byte;
  ParsedQuicVersion version;
  // nonce contains an optional, 32-byte nonce value. If not included in the
  // packet, |nonce| will be empty.
  DiversificationNonce* nonce;
  QuicPacketNumber packet_number;
  // Format of this header.
  PacketHeaderFormat form;
  // Short packet type is reflected in packet_number_length.
  QuicLongHeaderType long_packet_type;
  // Only valid if |has_possible_stateless_reset_token| is true.
  // Stores last 16 bytes of a this packet, used to check whether this packet is
  // a stateless reset packet on decryption failure.
  StatelessResetToken possible_stateless_reset_token;
  // Length of the retry token length variable length integer field,
  // carried only by v99 IETF Initial packets.
  quiche::QuicheVariableLengthIntegerLength retry_token_length_length;
  // Retry token, carried only by v99 IETF Initial packets.
  absl::string_view retry_token;
  // Length of the length variable length integer field,
  // carried only by v99 IETF Initial, 0-RTT and Handshake packets.
  quiche::QuicheVariableLengthIntegerLength length_length;
  // Length of the packet number and payload, carried only by v99 IETF Initial,
  // 0-RTT and Handshake packets. Also includes the length of the
  // diversification nonce in server to client 0-RTT packets.
  QuicByteCount remaining_packet_length;

  bool operator==(const QuicPacketHeader& other) const;
  bool operator!=(const QuicPacketHeader& other) const;
};

struct QUICHE_EXPORT QuicPublicResetPacket {
  QuicPublicResetPacket();
  explicit QuicPublicResetPacket(QuicConnectionId connection_id);

  QuicConnectionId connection_id;
  QuicPublicResetNonceProof nonce_proof;
  QuicSocketAddress client_address;
  // An arbitrary string to identify an endpoint. Used by clients to
  // differentiate traffic from Google servers vs Non-google servers.
  // Will not be used if empty().
  std::string endpoint_id;
};

struct QUICHE_EXPORT QuicVersionNegotiationPacket {
  QuicVersionNegotiationPacket();
  explicit QuicVersionNegotiationPacket(QuicConnectionId connection_id);
  QuicVersionNegotiationPacket(const QuicVersionNegotiationPacket& other);
  ~QuicVersionNegotiationPacket();

  QuicConnectionId connection_id;
  ParsedQuicVersionVector versions;
};

struct QUICHE_EXPORT QuicIetfStatelessResetPacket {
  QuicIetfStatelessResetPacket();
  QuicIetfStatelessResetPacket(const QuicPacketHeader& header,
                               StatelessResetToken token);
  QuicIetfStatelessResetPacket(const QuicIetfStatelessResetPacket& other);
  ~QuicIetfStatelessResetPacket();

  QuicPacketHeader header;
  StatelessResetToken stateless_reset_token;
};

class QUICHE_EXPORT QuicData {
 public:
  // Creates a QuicData from a buffer and length. Does not own the buffer.
  QuicData(const char* buffer, size_t length);
  // Creates a QuicData from a buffer and length,
  // optionally taking ownership of the buffer.
  QuicData(const char* buffer, size_t length, bool owns_buffer);
  // Creates a QuicData from a absl::string_view. Does not own the
  // buffer.
  QuicData(absl::string_view data);
  QuicData(const QuicData&) = delete;
  QuicData& operator=(const QuicData&) = delete;
  virtual ~QuicData();

  absl::string_view AsStringPiece() const {
    return absl::string_view(data(), length());
  }

  const char* data() const { return buffer_; }
  size_t length() const { return length_; }

 private:
  const char* buffer_;
  size_t length_;
  bool owns_buffer_;
};

class QUICHE_EXPORT QuicPacket : public QuicData {
 public:
  QuicPacket(
      char* buffer, size_t length, bool owns_buffer,
      uint8_t destination_connection_id_length,
      uint8_t source_connection_id_length, bool includes_version,
      bool includes_diversification_nonce,
      QuicPacketNumberLength packet_number_length,
      quiche::QuicheVariableLengthIntegerLength retry_token_length_length,
      QuicByteCount retry_token_length,
      quiche::QuicheVariableLengthIntegerLength length_length);
  QuicPacket(QuicTransportVersion version, char* buffer, size_t length,
             bool owns_buffer, const QuicPacketHeader& header);
  QuicPacket(const QuicPacket&) = delete;
  QuicPacket& operator=(const QuicPacket&) = delete;

  absl::string_view AssociatedData(QuicTransportVersion version) const;
  absl::string_view Plaintext(QuicTransportVersion version) const;

  char* mutable_data() { return buffer_; }

 private:
  char* buffer_;
  const uint8_t destination_connection_id_length_;
  const uint8_t source_connection_id_length_;
  const bool includes_version_;
  const bool includes_diversification_nonce_;
  const QuicPacketNumberLength packet_number_length_;
  const quiche::QuicheVariableLengthIntegerLength retry_token_length_length_;
  const QuicByteCount retry_token_length_;
  const quiche::QuicheVariableLengthIntegerLength length_length_;
};

class QUICHE_EXPORT QuicEncryptedPacket : public QuicData {
 public:
  // Creates a QuicEncryptedPacket from a buffer and length.
  // Does not own the buffer.
  QuicEncryptedPacket(const char* buffer, size_t length);
  // Creates a QuicEncryptedPacket from a buffer and length,
  // optionally taking ownership of the buffer.
  QuicEncryptedPacket(const char* buffer, size_t length, bool owns_buffer);
  // Creates a QuicEncryptedPacket from a absl::string_view.
  // Does not own the buffer.
  QuicEncryptedPacket(absl::string_view data);

  QuicEncryptedPacket(const QuicEncryptedPacket&) = delete;
  QuicEncryptedPacket& operator=(const QuicEncryptedPacket&) = delete;

  // Clones the packet into a new packet which owns the buffer.
  std::unique_ptr<QuicEncryptedPacket> Clone() const;

  // By default, gtest prints the raw bytes of an object. The bool data
  // member (in the base class QuicData) causes this object to have padding
  // bytes, which causes the default gtest object printer to read
  // uninitialize memory. So we need to teach gtest how to print this object.
  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const QuicEncryptedPacket& s);
};

namespace test {
class QuicReceivedPacketPeer;
}  // namespace test

// A received encrypted QUIC packet, with a recorded time of receipt.
class QUICHE_EXPORT QuicReceivedPacket : public QuicEncryptedPacket {
 public:
  QuicReceivedPacket(const char* buffer, size_t length, QuicTime receipt_time);
  QuicReceivedPacket(const char* buffer, size_t length, QuicTime receipt_time,
                     bool owns_buffer);
  QuicReceivedPacket(const char* buffer, size_t length, QuicTime receipt_time,
                     bool owns_buffer, int ttl, bool ttl_valid);
  QuicReceivedPacket(const char* buffer, size_t length, QuicTime receipt_time,
                     bool owns_buffer, int ttl, bool ttl_valid,
                     char* packet_headers, size_t headers_length,
                     bool owns_header_buffer);
  QuicReceivedPacket(const char* buffer, size_t length, QuicTime receipt_time,
                     bool owns_buffer, int ttl, bool ttl_valid,
                     char* packet_headers, size_t headers_length,
                     bool owns_header_buffer, QuicEcnCodepoint ecn_codepoint);
  QuicReceivedPacket(const char* buffer, size_t length, QuicTime receipt_time,
                     bool owns_buffer, int ttl, bool ttl_valid,
                     char* packet_headers, size_t headers_length,
                     bool owns_header_buffer, QuicEcnCodepoint ecn_codepoint,
                     uint32_t ipv6_flow_label);
  ~QuicReceivedPacket();
  QuicReceivedPacket(const QuicReceivedPacket&) = delete;
  QuicReceivedPacket& operator=(const QuicReceivedPacket&) = delete;

  // Clones the packet into a new packet which owns the buffer.
  std::unique_ptr<QuicReceivedPacket> Clone() const;

  // Returns the time at which the packet was received.
  QuicTime receipt_time() const { return receipt_time_; }

  // This is the TTL of the packet, assuming ttl_vaild_ is true.
  int ttl() const { return ttl_; }

  // Start of packet headers.
  char* packet_headers() const { return packet_headers_; }

  // Length of packet headers.
  int headers_length() const { return headers_length_; }

  QuicEcnCodepoint ecn_codepoint() const { return ecn_codepoint_; }

  // Returns the IPv6 flow label in host byte order if present, or 0 otherwise.
  uint32_t ipv6_flow_label() const { return ipv6_flow_label_; }

  // By default, gtest prints the raw bytes of an object. The bool data
  // member (in the base class QuicData) causes this object to have padding
  // bytes, which causes the default gtest object printer to read
  // uninitialize memory. So we need to teach gtest how to print this object.
  QUICHE_EXPORT friend std::ostream& operator<<(std::ostream& os,
                                                const QuicReceivedPacket& s);

 private:
  friend class test::QuicReceivedPacketPeer;

  const QuicTime receipt_time_;
  int ttl_;
  // Points to the start of packet headers.
  char* packet_headers_;
  // Length of packet headers.
  int headers_length_;
  // Whether owns the buffer for packet headers.
  bool owns_header_buffer_;
  QuicEcnCodepoint ecn_codepoint_;
  // IPv6 flow label.
  uint32_t ipv6_flow_label_;
};

// SerializedPacket contains information of a serialized(encrypted) packet.
//
// WARNING:
//
//   If you add a member field to this class, please make sure it is properly
//   copied in |CopySerializedPacket|.
//
struct QUICHE_EXPORT SerializedPacket {
  SerializedPacket(QuicPacketNumber packet_number,
                   QuicPacketNumberLength packet_number_length,
                   const char* encrypted_buffer,
                   QuicPacketLength encrypted_length, bool has_ack,
                   bool has_stop_waiting);

  // Copy constructor & assignment are deleted. Use |CopySerializedPacket| to
  // make a copy.
  SerializedPacket(const SerializedPacket& other) = delete;
  SerializedPacket& operator=(const SerializedPacket& other) = delete;
  SerializedPacket(SerializedPacket&& other);
  ~SerializedPacket();

  // TODO(wub): replace |encrypted_buffer|+|release_encrypted_buffer| by a
  // QuicOwnedPacketBuffer.
  // Not owned if |release_encrypted_buffer| is nullptr. Otherwise it is
  // released by |release_encrypted_buffer| on destruction.
  const char* encrypted_buffer;
  QuicPacketLength encrypted_length;
  std::function<void(const char*)> release_encrypted_buffer;

  QuicFrames retransmittable_frames;
  QuicFrames nonretransmittable_frames;
  IsHandshake has_crypto_handshake;
  QuicPacketNumber packet_number;
  QuicPacketNumberLength packet_number_length;
  EncryptionLevel encryption_level;
  // TODO(fayang): Remove has_ack and has_stop_waiting.
  bool has_ack;
  bool has_stop_waiting;
  bool has_ack_ecn = false;  // ack frame contains ECN counts.
  TransmissionType transmission_type;
  // The largest acked of the AckFrame in this packet if has_ack is true,
  // 0 otherwise.
  QuicPacketNumber largest_acked;
  // Indicates whether this packet has a copy of ack frame in
  // nonretransmittable_frames.
  bool has_ack_frame_copy;
  bool has_ack_frequency;
  bool has_message;
  SerializedPacketFate fate;
  QuicSocketAddress peer_address;
  // Sum of bytes from frames that are not retransmissions. This field is only
  // populated for packets with "mixed frames": at least one frame of a
  // retransmission type and at least one frame of NOT_RETRANSMISSION type.
  std::optional<QuicByteCount> bytes_not_retransmitted;
  // Only populated if encryption_level is ENCRYPTION_INITIAL.
  // TODO(b/265777524): remove this.
  std::optional<QuicPacketHeader> initial_header;
};

// Make a copy of |serialized| (including the underlying frames). |copy_buffer|
// indicates whether the encrypted buffer should be copied.
QUICHE_EXPORT SerializedPacket* CopySerializedPacket(
    const SerializedPacket& serialized,
    quiche::QuicheBufferAllocator* allocator, bool copy_buffer);

// Allocates a new char[] of size |packet.encrypted_length| and copies in
// |packet.encrypted_buffer|.
QUICHE_EXPORT char* CopyBuffer(const SerializedPacket& packet);
// Allocates a new char[] of size |encrypted_length| and copies in
// |encrypted_buffer|.
QUICHE_EXPORT char* CopyBuffer(const char* encrypted_buffer,
                               QuicPacketLength encrypted_length);

// Context for an incoming packet.
struct QUICHE_EXPORT QuicPerPacketContext {
  virtual ~QuicPerPacketContext() {}
};

// ReceivedPacketInfo comprises information obtained by parsing the unencrypted
// bytes of a received packet.
struct QUICHE_EXPORT ReceivedPacketInfo {
  ReceivedPacketInfo(const QuicSocketAddress& self_address,
                     const QuicSocketAddress& peer_address,
                     const QuicReceivedPacket& packet);
  ReceivedPacketInfo(const ReceivedPacketInfo& other) = default;

  ~ReceivedPacketInfo();

  std::string ToString() const;

  QUICHE_EXPORT friend std::ostream& operator<<(
      std::ostream& os, const ReceivedPacketInfo& packet_info);

  const QuicSocketAddress& self_address;
  const QuicSocketAddress& peer_address;
  const QuicReceivedPacket& packet;

  PacketHeaderFormat form;
  // This is only used if the form is IETF_QUIC_LONG_HEADER_PACKET.
  QuicLongHeaderType long_packet_type;
  bool version_flag;
  bool use_length_prefix;
  QuicVersionLabel version_label;
  ParsedQuicVersion version;
  QuicConnectionId destination_connection_id;
  QuicConnectionId source_connection_id;
  std::optional<absl::string_view> retry_token;
};

// Information about a packet sent by the dispatcher.
struct QUICHE_EXPORT DispatcherSentPacket {
  // The packet number sent, in response to |received_packet_number|.
  QuicPacketNumber packet_number;
  // The packet number received.
  QuicPacketNumber received_packet_number;
  // The largest acked packet number in all received packets, including
  // |received_packet_number|.
  QuicPacketNumber largest_acked;
  QuicTime sent_time = QuicTime::Zero();
  QuicPacketLength bytes_sent = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PACKETS_H_
