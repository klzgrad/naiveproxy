// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_packets.h"

#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"

namespace quic {

QuicConnectionId GetServerConnectionIdAsRecipient(
    const QuicPacketHeader& header,
    Perspective perspective) {
  if (perspective == Perspective::IS_SERVER) {
    return header.destination_connection_id;
  }
  return header.source_connection_id;
}

QuicConnectionId GetClientConnectionIdAsRecipient(
    const QuicPacketHeader& header,
    Perspective perspective) {
  if (perspective == Perspective::IS_CLIENT) {
    return header.destination_connection_id;
  }
  return header.source_connection_id;
}

QuicConnectionId GetServerConnectionIdAsSender(const QuicPacketHeader& header,
                                               Perspective perspective) {
  if (perspective == Perspective::IS_CLIENT) {
    return header.destination_connection_id;
  }
  return header.source_connection_id;
}

QuicConnectionIdIncluded GetServerConnectionIdIncludedAsSender(
    const QuicPacketHeader& header,
    Perspective perspective) {
  if (perspective == Perspective::IS_CLIENT) {
    return header.destination_connection_id_included;
  }
  return header.source_connection_id_included;
}

QuicConnectionId GetClientConnectionIdAsSender(const QuicPacketHeader& header,
                                               Perspective perspective) {
  if (perspective == Perspective::IS_CLIENT) {
    return header.source_connection_id;
  }
  return header.destination_connection_id;
}

QuicConnectionIdIncluded GetClientConnectionIdIncludedAsSender(
    const QuicPacketHeader& header,
    Perspective perspective) {
  if (perspective == Perspective::IS_CLIENT) {
    return header.source_connection_id_included;
  }
  return header.destination_connection_id_included;
}

QuicConnectionIdLength GetIncludedConnectionIdLength(
    QuicConnectionId connection_id,
    QuicConnectionIdIncluded connection_id_included) {
  DCHECK(connection_id_included == CONNECTION_ID_PRESENT ||
         connection_id_included == CONNECTION_ID_ABSENT);
  return connection_id_included == CONNECTION_ID_PRESENT
             ? static_cast<QuicConnectionIdLength>(connection_id.length())
             : PACKET_0BYTE_CONNECTION_ID;
}

QuicConnectionIdLength GetIncludedDestinationConnectionIdLength(
    const QuicPacketHeader& header) {
  return GetIncludedConnectionIdLength(
      header.destination_connection_id,
      header.destination_connection_id_included);
}

QuicConnectionIdLength GetIncludedSourceConnectionIdLength(
    const QuicPacketHeader& header) {
  return GetIncludedConnectionIdLength(header.source_connection_id,
                                       header.source_connection_id_included);
}

size_t GetPacketHeaderSize(QuicTransportVersion version,
                           const QuicPacketHeader& header) {
  return GetPacketHeaderSize(
      version, GetIncludedDestinationConnectionIdLength(header),
      GetIncludedSourceConnectionIdLength(header), header.version_flag,
      header.nonce != nullptr, header.packet_number_length,
      header.retry_token_length_length, header.retry_token.length(),
      header.length_length);
}

size_t GetPacketHeaderSize(
    QuicTransportVersion version,
    QuicConnectionIdLength destination_connection_id_length,
    QuicConnectionIdLength source_connection_id_length,
    bool include_version,
    bool include_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    QuicVariableLengthIntegerLength retry_token_length_length,
    QuicByteCount retry_token_length,
    QuicVariableLengthIntegerLength length_length) {
  if (VersionHasIetfInvariantHeader(version)) {
    if (include_version) {
      // Long header.
      size_t size = kPacketHeaderTypeSize + kConnectionIdLengthSize +
                    destination_connection_id_length +
                    source_connection_id_length + packet_number_length +
                    kQuicVersionSize;
      if (include_diversification_nonce) {
        size += kDiversificationNonceSize;
      }
      if (VersionHasLengthPrefixedConnectionIds(version)) {
        size += kConnectionIdLengthSize;
      }
      DCHECK(QuicVersionHasLongHeaderLengths(version) ||
             !GetQuicReloadableFlag(quic_fix_get_packet_header_size) ||
             retry_token_length_length + retry_token_length + length_length ==
                 0);
      if (QuicVersionHasLongHeaderLengths(version) ||
          !GetQuicReloadableFlag(quic_fix_get_packet_header_size)) {
        QUIC_RELOADABLE_FLAG_COUNT_N(quic_fix_get_packet_header_size, 1, 3);
        size += retry_token_length_length + retry_token_length + length_length;
      }
      return size;
    }
    // Short header.
    return kPacketHeaderTypeSize + destination_connection_id_length +
           packet_number_length;
  }
  // Google QUIC versions <= 43 can only carry one connection ID.
  DCHECK(destination_connection_id_length == 0 ||
         source_connection_id_length == 0);
  return kPublicFlagsSize + destination_connection_id_length +
         source_connection_id_length +
         (include_version ? kQuicVersionSize : 0) + packet_number_length +
         (include_diversification_nonce ? kDiversificationNonceSize : 0);
}

size_t GetStartOfEncryptedData(QuicTransportVersion version,
                               const QuicPacketHeader& header) {
  return GetPacketHeaderSize(version, header);
}

size_t GetStartOfEncryptedData(
    QuicTransportVersion version,
    QuicConnectionIdLength destination_connection_id_length,
    QuicConnectionIdLength source_connection_id_length,
    bool include_version,
    bool include_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    QuicVariableLengthIntegerLength retry_token_length_length,
    QuicByteCount retry_token_length,
    QuicVariableLengthIntegerLength length_length) {
  // Encryption starts before private flags.
  return GetPacketHeaderSize(
      version, destination_connection_id_length, source_connection_id_length,
      include_version, include_diversification_nonce, packet_number_length,
      retry_token_length_length, retry_token_length, length_length);
}

QuicPacketHeader::QuicPacketHeader()
    : destination_connection_id(EmptyQuicConnectionId()),
      destination_connection_id_included(CONNECTION_ID_PRESENT),
      source_connection_id(EmptyQuicConnectionId()),
      source_connection_id_included(CONNECTION_ID_ABSENT),
      reset_flag(false),
      version_flag(false),
      has_possible_stateless_reset_token(false),
      packet_number_length(PACKET_4BYTE_PACKET_NUMBER),
      version(UnsupportedQuicVersion()),
      nonce(nullptr),
      form(GOOGLE_QUIC_PACKET),
      long_packet_type(INITIAL),
      possible_stateless_reset_token(0),
      retry_token_length_length(VARIABLE_LENGTH_INTEGER_LENGTH_0),
      retry_token(QuicStringPiece()),
      length_length(VARIABLE_LENGTH_INTEGER_LENGTH_0),
      remaining_packet_length(0) {}

QuicPacketHeader::QuicPacketHeader(const QuicPacketHeader& other) = default;

QuicPacketHeader::~QuicPacketHeader() {}

QuicPublicResetPacket::QuicPublicResetPacket()
    : connection_id(EmptyQuicConnectionId()), nonce_proof(0) {}

QuicPublicResetPacket::QuicPublicResetPacket(QuicConnectionId connection_id)
    : connection_id(connection_id), nonce_proof(0) {}

QuicVersionNegotiationPacket::QuicVersionNegotiationPacket()
    : connection_id(EmptyQuicConnectionId()) {}

QuicVersionNegotiationPacket::QuicVersionNegotiationPacket(
    QuicConnectionId connection_id)
    : connection_id(connection_id) {}

QuicVersionNegotiationPacket::QuicVersionNegotiationPacket(
    const QuicVersionNegotiationPacket& other) = default;

QuicVersionNegotiationPacket::~QuicVersionNegotiationPacket() {}

QuicIetfStatelessResetPacket::QuicIetfStatelessResetPacket()
    : stateless_reset_token(0) {}

QuicIetfStatelessResetPacket::QuicIetfStatelessResetPacket(
    const QuicPacketHeader& header,
    QuicUint128 token)
    : header(header), stateless_reset_token(token) {}

QuicIetfStatelessResetPacket::QuicIetfStatelessResetPacket(
    const QuicIetfStatelessResetPacket& other) = default;

QuicIetfStatelessResetPacket::~QuicIetfStatelessResetPacket() {}

std::ostream& operator<<(std::ostream& os, const QuicPacketHeader& header) {
  os << "{ destination_connection_id: " << header.destination_connection_id
     << " ("
     << (header.destination_connection_id_included == CONNECTION_ID_PRESENT
             ? "present"
             : "absent")
     << "), source_connection_id: " << header.source_connection_id << " ("
     << (header.source_connection_id_included == CONNECTION_ID_PRESENT
             ? "present"
             : "absent")
     << "), packet_number_length: " << header.packet_number_length
     << ", reset_flag: " << header.reset_flag
     << ", version_flag: " << header.version_flag;
  if (header.version_flag) {
    os << ", version: " << ParsedQuicVersionToString(header.version);
    if (header.long_packet_type != INVALID_PACKET_TYPE) {
      os << ", long_packet_type: "
         << QuicUtils::QuicLongHeaderTypetoString(header.long_packet_type);
    }
    if (header.retry_token_length_length != VARIABLE_LENGTH_INTEGER_LENGTH_0) {
      os << ", retry_token_length_length: "
         << static_cast<int>(header.retry_token_length_length);
    }
    if (header.retry_token.length() != 0) {
      os << ", retry_token_length: " << header.retry_token.length();
    }
    if (header.length_length != VARIABLE_LENGTH_INTEGER_LENGTH_0) {
      os << ", length_length: " << static_cast<int>(header.length_length);
    }
    if (header.remaining_packet_length != 0) {
      os << ", remaining_packet_length: " << header.remaining_packet_length;
    }
  }
  if (header.nonce != nullptr) {
    os << ", diversification_nonce: "
       << QuicTextUtils::HexEncode(
              QuicStringPiece(header.nonce->data(), header.nonce->size()));
  }
  os << ", packet_number: " << header.packet_number << " }\n";
  return os;
}

QuicData::QuicData(const char* buffer, size_t length)
    : buffer_(buffer), length_(length), owns_buffer_(false) {}

QuicData::QuicData(const char* buffer, size_t length, bool owns_buffer)
    : buffer_(buffer), length_(length), owns_buffer_(owns_buffer) {}

QuicData::QuicData(QuicStringPiece packet_data)
    : buffer_(packet_data.data()),
      length_(packet_data.length()),
      owns_buffer_(false) {}

QuicData::~QuicData() {
  if (owns_buffer_) {
    delete[] const_cast<char*>(buffer_);
  }
}

QuicPacket::QuicPacket(
    char* buffer,
    size_t length,
    bool owns_buffer,
    QuicConnectionIdLength destination_connection_id_length,
    QuicConnectionIdLength source_connection_id_length,
    bool includes_version,
    bool includes_diversification_nonce,
    QuicPacketNumberLength packet_number_length,
    QuicVariableLengthIntegerLength retry_token_length_length,
    QuicByteCount retry_token_length,
    QuicVariableLengthIntegerLength length_length)
    : QuicData(buffer, length, owns_buffer),
      buffer_(buffer),
      destination_connection_id_length_(destination_connection_id_length),
      source_connection_id_length_(source_connection_id_length),
      includes_version_(includes_version),
      includes_diversification_nonce_(includes_diversification_nonce),
      packet_number_length_(packet_number_length),
      retry_token_length_length_(retry_token_length_length),
      retry_token_length_(retry_token_length),
      length_length_(length_length) {}

QuicPacket::QuicPacket(QuicTransportVersion /*version*/,
                       char* buffer,
                       size_t length,
                       bool owns_buffer,
                       const QuicPacketHeader& header)
    : QuicPacket(buffer,
                 length,
                 owns_buffer,
                 GetIncludedDestinationConnectionIdLength(header),
                 GetIncludedSourceConnectionIdLength(header),
                 header.version_flag,
                 header.nonce != nullptr,
                 header.packet_number_length,
                 header.retry_token_length_length,
                 header.retry_token.length(),
                 header.length_length) {}

QuicEncryptedPacket::QuicEncryptedPacket(const char* buffer, size_t length)
    : QuicData(buffer, length) {}

QuicEncryptedPacket::QuicEncryptedPacket(const char* buffer,
                                         size_t length,
                                         bool owns_buffer)
    : QuicData(buffer, length, owns_buffer) {}

QuicEncryptedPacket::QuicEncryptedPacket(QuicStringPiece data)
    : QuicData(data) {}

std::unique_ptr<QuicEncryptedPacket> QuicEncryptedPacket::Clone() const {
  char* buffer = new char[this->length()];
  memcpy(buffer, this->data(), this->length());
  return QuicMakeUnique<QuicEncryptedPacket>(buffer, this->length(), true);
}

std::ostream& operator<<(std::ostream& os, const QuicEncryptedPacket& s) {
  os << s.length() << "-byte data";
  return os;
}

QuicReceivedPacket::QuicReceivedPacket(const char* buffer,
                                       size_t length,
                                       QuicTime receipt_time)
    : QuicReceivedPacket(buffer,
                         length,
                         receipt_time,
                         false /* owns_buffer */) {}

QuicReceivedPacket::QuicReceivedPacket(const char* buffer,
                                       size_t length,
                                       QuicTime receipt_time,
                                       bool owns_buffer)
    : QuicReceivedPacket(buffer,
                         length,
                         receipt_time,
                         owns_buffer,
                         0 /* ttl */,
                         true /* ttl_valid */) {}

QuicReceivedPacket::QuicReceivedPacket(const char* buffer,
                                       size_t length,
                                       QuicTime receipt_time,
                                       bool owns_buffer,
                                       int ttl,
                                       bool ttl_valid)
    : quic::QuicReceivedPacket(buffer,
                               length,
                               receipt_time,
                               owns_buffer,
                               ttl,
                               ttl_valid,
                               nullptr /* packet_headers */,
                               0 /* headers_length */,
                               false /* owns_header_buffer */) {}

QuicReceivedPacket::QuicReceivedPacket(const char* buffer,
                                       size_t length,
                                       QuicTime receipt_time,
                                       bool owns_buffer,
                                       int ttl,
                                       bool ttl_valid,
                                       char* packet_headers,
                                       size_t headers_length,
                                       bool owns_header_buffer)
    : QuicEncryptedPacket(buffer, length, owns_buffer),
      receipt_time_(receipt_time),
      ttl_(ttl_valid ? ttl : -1),
      packet_headers_(packet_headers),
      headers_length_(headers_length),
      owns_header_buffer_(owns_header_buffer) {}

QuicReceivedPacket::~QuicReceivedPacket() {
  if (owns_header_buffer_) {
    delete[] static_cast<char*>(packet_headers_);
  }
}

std::unique_ptr<QuicReceivedPacket> QuicReceivedPacket::Clone() const {
  char* buffer = new char[this->length()];
  memcpy(buffer, this->data(), this->length());
  if (this->packet_headers()) {
    char* headers_buffer = new char[this->headers_length()];
    memcpy(headers_buffer, this->packet_headers(), this->headers_length());
    return QuicMakeUnique<QuicReceivedPacket>(
        buffer, this->length(), receipt_time(), true, ttl(), ttl() >= 0,
        headers_buffer, this->headers_length(), true);
  }

  return QuicMakeUnique<QuicReceivedPacket>(
      buffer, this->length(), receipt_time(), true, ttl(), ttl() >= 0);
}

std::ostream& operator<<(std::ostream& os, const QuicReceivedPacket& s) {
  os << s.length() << "-byte data";
  return os;
}

QuicStringPiece QuicPacket::AssociatedData(QuicTransportVersion version) const {
  return QuicStringPiece(
      data(),
      GetStartOfEncryptedData(version, destination_connection_id_length_,
                              source_connection_id_length_, includes_version_,
                              includes_diversification_nonce_,
                              packet_number_length_, retry_token_length_length_,
                              retry_token_length_, length_length_));
}

QuicStringPiece QuicPacket::Plaintext(QuicTransportVersion version) const {
  const size_t start_of_encrypted_data = GetStartOfEncryptedData(
      version, destination_connection_id_length_, source_connection_id_length_,
      includes_version_, includes_diversification_nonce_, packet_number_length_,
      retry_token_length_length_, retry_token_length_, length_length_);
  return QuicStringPiece(data() + start_of_encrypted_data,
                         length() - start_of_encrypted_data);
}

SerializedPacket::SerializedPacket(QuicPacketNumber packet_number,
                                   QuicPacketNumberLength packet_number_length,
                                   const char* encrypted_buffer,
                                   QuicPacketLength encrypted_length,
                                   bool has_ack,
                                   bool has_stop_waiting)
    : encrypted_buffer(encrypted_buffer),
      encrypted_length(encrypted_length),
      has_crypto_handshake(NOT_HANDSHAKE),
      num_padding_bytes(0),
      packet_number(packet_number),
      packet_number_length(packet_number_length),
      encryption_level(ENCRYPTION_INITIAL),
      has_ack(has_ack),
      has_stop_waiting(has_stop_waiting),
      transmission_type(NOT_RETRANSMISSION) {}

SerializedPacket::SerializedPacket(const SerializedPacket& other) = default;

SerializedPacket& SerializedPacket::operator=(const SerializedPacket& other) =
    default;

SerializedPacket::SerializedPacket(SerializedPacket&& other)
    : encrypted_buffer(other.encrypted_buffer),
      encrypted_length(other.encrypted_length),
      has_crypto_handshake(other.has_crypto_handshake),
      num_padding_bytes(other.num_padding_bytes),
      packet_number(other.packet_number),
      packet_number_length(other.packet_number_length),
      encryption_level(other.encryption_level),
      has_ack(other.has_ack),
      has_stop_waiting(other.has_stop_waiting),
      transmission_type(other.transmission_type),
      original_packet_number(other.original_packet_number),
      largest_acked(other.largest_acked) {
  retransmittable_frames.swap(other.retransmittable_frames);
}

SerializedPacket::~SerializedPacket() {}

void ClearSerializedPacket(SerializedPacket* serialized_packet) {
  if (!serialized_packet->retransmittable_frames.empty()) {
    DeleteFrames(&serialized_packet->retransmittable_frames);
  }
  serialized_packet->encrypted_buffer = nullptr;
  serialized_packet->encrypted_length = 0;
  serialized_packet->largest_acked.Clear();
}

char* CopyBuffer(const SerializedPacket& packet) {
  char* dst_buffer = new char[packet.encrypted_length];
  memcpy(dst_buffer, packet.encrypted_buffer, packet.encrypted_length);
  return dst_buffer;
}

ReceivedPacketInfo::ReceivedPacketInfo(const QuicSocketAddress& self_address,
                                       const QuicSocketAddress& peer_address,
                                       const QuicReceivedPacket& packet)
    : self_address(self_address),
      peer_address(peer_address),
      packet(packet),
      form(GOOGLE_QUIC_PACKET),
      version_flag(false),
      use_length_prefix(false),
      version_label(0),
      version(PROTOCOL_UNSUPPORTED, QUIC_VERSION_UNSUPPORTED),
      destination_connection_id(EmptyQuicConnectionId()),
      source_connection_id(EmptyQuicConnectionId()) {}

ReceivedPacketInfo::~ReceivedPacketInfo() {}

std::string ReceivedPacketInfo::ToString() const {
  std::string output =
      QuicStrCat("{ self_address: ", self_address.ToString(),
                 ", peer_address: ", peer_address.ToString(),
                 ", packet_length: ", packet.length(),
                 ", header_format: ", form, ", version_flag: ", version_flag);
  if (version_flag) {
    QuicStrAppend(&output, ", version: ", ParsedQuicVersionToString(version));
  }
  QuicStrAppend(
      &output,
      ", destination_connection_id: ", destination_connection_id.ToString(),
      ", source_connection_id: ", source_connection_id.ToString(), " }\n");
  return output;
}

std::ostream& operator<<(std::ostream& os,
                         const ReceivedPacketInfo& packet_info) {
  os << packet_info.ToString();
  return os;
}

}  // namespace quic
