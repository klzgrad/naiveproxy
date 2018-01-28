// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_PACKETS_H_
#define NET_QUIC_CORE_QUIC_PACKETS_H_

#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "net/base/iovec.h"
#include "net/quic/core/frames/quic_frame.h"
#include "net/quic/core/quic_ack_listener_interface.h"
#include "net/quic/core/quic_bandwidth.h"
#include "net/quic/core/quic_constants.h"
#include "net/quic/core/quic_error_codes.h"
#include "net/quic/core/quic_time.h"
#include "net/quic/core/quic_types.h"
#include "net/quic/core/quic_versions.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_socket_address.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class QuicPacket;
struct QuicPacketHeader;

// Size in bytes of the data packet header.
QUIC_EXPORT_PRIVATE size_t GetPacketHeaderSize(QuicTransportVersion version,
                                               const QuicPacketHeader& header);

QUIC_EXPORT_PRIVATE size_t
GetPacketHeaderSize(QuicTransportVersion version,
                    QuicConnectionIdLength connection_id_length,
                    bool include_version,
                    bool include_diversification_nonce,
                    QuicPacketNumberLength packet_number_length);

// Index of the first byte in a QUIC packet of encrypted data.
QUIC_EXPORT_PRIVATE size_t
GetStartOfEncryptedData(QuicTransportVersion version,
                        const QuicPacketHeader& header);

QUIC_EXPORT_PRIVATE size_t
GetStartOfEncryptedData(QuicTransportVersion version,
                        QuicConnectionIdLength connection_id_length,
                        bool include_version,
                        bool include_diversification_nonce,
                        QuicPacketNumberLength packet_number_length);

struct QUIC_EXPORT_PRIVATE QuicPacketPublicHeader {
  QuicPacketPublicHeader();
  QuicPacketPublicHeader(const QuicPacketPublicHeader& other);
  ~QuicPacketPublicHeader();

  // Universal header. All QuicPacket headers will have a connection_id and
  // public flags.
  QuicConnectionId connection_id;
  QuicConnectionIdLength connection_id_length;
  bool reset_flag;
  bool version_flag;
  QuicPacketNumberLength packet_number_length;
  QuicTransportVersionVector versions;
  // nonce contains an optional, 32-byte nonce value. If not included in the
  // packet, |nonce| will be empty.
  DiversificationNonce* nonce;
};

// Header for Data packets.
struct QUIC_EXPORT_PRIVATE QuicPacketHeader {
  QuicPacketHeader();
  explicit QuicPacketHeader(const QuicPacketPublicHeader& header);
  QuicPacketHeader(const QuicPacketHeader& other);

  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
      const QuicPacketHeader& s);

  QuicPacketPublicHeader public_header;
  QuicPacketNumber packet_number;
};

struct QUIC_EXPORT_PRIVATE QuicPublicResetPacket {
  QuicPublicResetPacket();
  explicit QuicPublicResetPacket(const QuicPacketPublicHeader& header);

  QuicPacketPublicHeader public_header;
  QuicPublicResetNonceProof nonce_proof;
  QuicSocketAddress client_address;
};

typedef QuicPacketPublicHeader QuicVersionNegotiationPacket;

class QUIC_EXPORT_PRIVATE QuicData {
 public:
  QuicData(const char* buffer, size_t length);
  QuicData(const char* buffer, size_t length, bool owns_buffer);
  virtual ~QuicData();

  QuicStringPiece AsStringPiece() const {
    return QuicStringPiece(data(), length());
  }

  const char* data() const { return buffer_; }
  size_t length() const { return length_; }

 private:
  const char* buffer_;
  size_t length_;
  bool owns_buffer_;

  DISALLOW_COPY_AND_ASSIGN(QuicData);
};

class QUIC_EXPORT_PRIVATE QuicPacket : public QuicData {
 public:
  // TODO(fayang): 3 fields from public header are passed in as arguments.
  // Consider to add a convenience method which directly accepts the entire
  // public header.
  QuicPacket(char* buffer,
             size_t length,
             bool owns_buffer,
             QuicConnectionIdLength connection_id_length,
             bool includes_version,
             bool includes_diversification_nonce,
             QuicPacketNumberLength packet_number_length);

  QuicStringPiece AssociatedData(QuicTransportVersion version) const;
  QuicStringPiece Plaintext(QuicTransportVersion version) const;

  char* mutable_data() { return buffer_; }

 private:
  char* buffer_;
  const QuicConnectionIdLength connection_id_length_;
  const bool includes_version_;
  const bool includes_diversification_nonce_;
  const QuicPacketNumberLength packet_number_length_;

  DISALLOW_COPY_AND_ASSIGN(QuicPacket);
};

class QUIC_EXPORT_PRIVATE QuicEncryptedPacket : public QuicData {
 public:
  QuicEncryptedPacket(const char* buffer, size_t length);
  QuicEncryptedPacket(const char* buffer, size_t length, bool owns_buffer);

  // Clones the packet into a new packet which owns the buffer.
  std::unique_ptr<QuicEncryptedPacket> Clone() const;

  // By default, gtest prints the raw bytes of an object. The bool data
  // member (in the base class QuicData) causes this object to have padding
  // bytes, which causes the default gtest object printer to read
  // uninitialize memory. So we need to teach gtest how to print this object.
  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
      const QuicEncryptedPacket& s);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicEncryptedPacket);
};

// A received encrypted QUIC packet, with a recorded time of receipt.
class QUIC_EXPORT_PRIVATE QuicReceivedPacket : public QuicEncryptedPacket {
 public:
  QuicReceivedPacket(const char* buffer, size_t length, QuicTime receipt_time);
  QuicReceivedPacket(const char* buffer,
                     size_t length,
                     QuicTime receipt_time,
                     bool owns_buffer);
  QuicReceivedPacket(const char* buffer,
                     size_t length,
                     QuicTime receipt_time,
                     bool owns_buffer,
                     int ttl,
                     bool ttl_valid);

  // Clones the packet into a new packet which owns the buffer.
  std::unique_ptr<QuicReceivedPacket> Clone() const;

  // Returns the time at which the packet was received.
  QuicTime receipt_time() const { return receipt_time_; }

  // This is the TTL of the packet, assuming ttl_vaild_ is true.
  int ttl() const { return ttl_; }

  // By default, gtest prints the raw bytes of an object. The bool data
  // member (in the base class QuicData) causes this object to have padding
  // bytes, which causes the default gtest object printer to read
  // uninitialize memory. So we need to teach gtest how to print this object.
  QUIC_EXPORT_PRIVATE friend std::ostream& operator<<(
      std::ostream& os,
      const QuicReceivedPacket& s);

 private:
  const QuicTime receipt_time_;
  int ttl_;

  DISALLOW_COPY_AND_ASSIGN(QuicReceivedPacket);
};

struct QUIC_EXPORT_PRIVATE SerializedPacket {
  SerializedPacket(QuicPacketNumber packet_number,
                   QuicPacketNumberLength packet_number_length,
                   const char* encrypted_buffer,
                   QuicPacketLength encrypted_length,
                   bool has_ack,
                   bool has_stop_waiting);
  SerializedPacket(const SerializedPacket& other);
  SerializedPacket& operator=(const SerializedPacket& other);
  SerializedPacket(SerializedPacket&& other);
  ~SerializedPacket();

  // Not owned.
  const char* encrypted_buffer;
  QuicPacketLength encrypted_length;
  QuicFrames retransmittable_frames;
  IsHandshake has_crypto_handshake;
  // -1: full padding to the end of a max-sized packet
  //  0: no padding
  //  otherwise: only pad up to num_padding_bytes bytes
  int16_t num_padding_bytes;
  QuicPacketNumber packet_number;
  QuicPacketNumberLength packet_number_length;
  EncryptionLevel encryption_level;
  bool has_ack;
  bool has_stop_waiting;
  TransmissionType transmission_type;
  QuicPacketNumber original_packet_number;
  // The largest acked of the AckFrame in this packet if has_ack is true,
  // 0 otherwise.
  QuicPacketNumber largest_acked;

  // Optional notifiers which will be informed when this packet has been ACKed.
  std::list<AckListenerWrapper> listeners;
};

// Deletes and clears all the frames and the packet from serialized packet.
QUIC_EXPORT_PRIVATE void ClearSerializedPacket(
    SerializedPacket* serialized_packet);

// Allocates a new char[] of size |packet.encrypted_length| and copies in
// |packet.encrypted_buffer|.
QUIC_EXPORT_PRIVATE char* CopyBuffer(const SerializedPacket& packet);

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_PACKETS_H_
