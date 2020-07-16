// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_UTILS_H_
#define QUICHE_QUIC_CORE_QUIC_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <type_traits>

#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/frames/quic_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_iovec.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_uint128.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicUtils {
 public:
  QuicUtils() = delete;

  // Returns the 64 bit FNV1a hash of the data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static uint64_t FNV1a_64_Hash(quiche::QuicheStringPiece data);

  // Returns the 128 bit FNV1a hash of the data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static QuicUint128 FNV1a_128_Hash(quiche::QuicheStringPiece data);

  // Returns the 128 bit FNV1a hash of the two sequences of data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static QuicUint128 FNV1a_128_Hash_Two(quiche::QuicheStringPiece data1,
                                        quiche::QuicheStringPiece data2);

  // Returns the 128 bit FNV1a hash of the three sequences of data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static QuicUint128 FNV1a_128_Hash_Three(quiche::QuicheStringPiece data1,
                                          quiche::QuicheStringPiece data2,
                                          quiche::QuicheStringPiece data3);

  // SerializeUint128 writes the first 96 bits of |v| in little-endian form
  // to |out|.
  static void SerializeUint128Short(QuicUint128 v, uint8_t* out);

  // Returns AddressChangeType as a string.
  static std::string AddressChangeTypeToString(AddressChangeType type);

  // Returns SentPacketState as a char*.
  static const char* SentPacketStateToString(SentPacketState state);

  // Returns QuicLongHeaderType as a char*.
  static const char* QuicLongHeaderTypetoString(QuicLongHeaderType type);

  // Returns AckResult as a char*.
  static const char* AckResultToString(AckResult result);

  // Determines and returns change type of address change from |old_address| to
  // |new_address|.
  static AddressChangeType DetermineAddressChangeType(
      const QuicSocketAddress& old_address,
      const QuicSocketAddress& new_address);

  // Copies |buffer_length| bytes from iov starting at offset |iov_offset| into
  // buffer. |iov| must be at least iov_offset+length total length and buffer
  // must be at least |length| long.
  static void CopyToBuffer(const struct iovec* iov,
                           int iov_count,
                           size_t iov_offset,
                           size_t buffer_length,
                           char* buffer);

  // Creates an iovec pointing to the same data as |data|.
  static struct iovec MakeIovec(quiche::QuicheStringPiece data);

  // Returns the opposite Perspective of the |perspective| passed in.
  static constexpr Perspective InvertPerspective(Perspective perspective) {
    return perspective == Perspective::IS_CLIENT ? Perspective::IS_SERVER
                                                 : Perspective::IS_CLIENT;
  }

  // Returns true if a packet is ackable. A packet is unackable if it can never
  // be acked. Occurs when a packet is never sent, after it is acknowledged
  // once, or if it's a crypto packet we never expect to receive an ack for.
  static bool IsAckable(SentPacketState state);

  // Returns true if frame with |type| is retransmittable. A retransmittable
  // frame should be retransmitted if it is detected as lost.
  static bool IsRetransmittableFrame(QuicFrameType type);

  // Returns true if |frame| is a handshake frame in version |version|.
  static bool IsHandshakeFrame(const QuicFrame& frame,
                               QuicTransportVersion transport_version);

  // Return true if any frame in |frames| is of |type|.
  static bool ContainsFrameType(const QuicFrames& frames, QuicFrameType type);

  // Returns packet state corresponding to |retransmission_type|.
  static SentPacketState RetransmissionTypeToPacketState(
      TransmissionType retransmission_type);

  // Returns true if header with |first_byte| is considered as an IETF QUIC
  // packet header. This only works on the server.
  static bool IsIetfPacketHeader(uint8_t first_byte);

  // Returns true if header with |first_byte| is considered as an IETF QUIC
  // short packet header.
  static bool IsIetfPacketShortHeader(uint8_t first_byte);

  // Returns ID to denote an invalid stream of |version|.
  static QuicStreamId GetInvalidStreamId(QuicTransportVersion version);

  // Returns crypto stream ID of |version|.
  static QuicStreamId GetCryptoStreamId(QuicTransportVersion version);

  // Returns whether |id| is the stream ID for the crypto stream. If |version|
  // is a version where crypto data doesn't go over stream frames, this function
  // will always return false.
  static bool IsCryptoStreamId(QuicTransportVersion version, QuicStreamId id);

  // Returns headers stream ID of |version|.
  static QuicStreamId GetHeadersStreamId(QuicTransportVersion version);

  // Returns true if |id| is considered as client initiated stream ID.
  static bool IsClientInitiatedStreamId(QuicTransportVersion version,
                                        QuicStreamId id);

  // Returns true if |id| is considered as server initiated stream ID.
  static bool IsServerInitiatedStreamId(QuicTransportVersion version,
                                        QuicStreamId id);

  // Returns true if the stream ID represents a stream initiated by the
  // provided perspective.
  static bool IsOutgoingStreamId(ParsedQuicVersion version,
                                 QuicStreamId id,
                                 Perspective perspective);

  // Returns true if |id| is considered as bidirectional stream ID. Only used in
  // v99.
  static bool IsBidirectionalStreamId(QuicStreamId id);

  // Returns stream type.  Either |perspective| or |peer_initiated| would be
  // enough together with |id|.  This method enforces that the three parameters
  // are consistent.  Only used in v99.
  static StreamType GetStreamType(QuicStreamId id,
                                  Perspective perspective,
                                  bool peer_initiated);

  // Returns the delta between consecutive stream IDs of the same type.
  static QuicStreamId StreamIdDelta(QuicTransportVersion version);

  // Returns the first initiated bidirectional stream ID of |perspective|.
  static QuicStreamId GetFirstBidirectionalStreamId(
      QuicTransportVersion version,
      Perspective perspective);

  // Returns the first initiated unidirectional stream ID of |perspective|.
  static QuicStreamId GetFirstUnidirectionalStreamId(
      QuicTransportVersion version,
      Perspective perspective);

  // Generates a 64bit connection ID derived from the input connection ID.
  // This is guaranteed to be deterministic (calling this method with two
  // connection IDs that are equal is guaranteed to produce the same result).
  static QuicConnectionId CreateReplacementConnectionId(
      QuicConnectionId connection_id);

  // Generates a random 64bit connection ID.
  static QuicConnectionId CreateRandomConnectionId();

  // Generates a random 64bit connection ID using the provided QuicRandom.
  static QuicConnectionId CreateRandomConnectionId(QuicRandom* random);

  // Generates a random connection ID of the given length.
  static QuicConnectionId CreateRandomConnectionId(
      uint8_t connection_id_length);

  // Generates a random connection ID of the given length using the provided
  // QuicRandom.
  static QuicConnectionId CreateRandomConnectionId(uint8_t connection_id_length,
                                                   QuicRandom* random);

  // Returns true if the connection ID length is valid for this QUIC version.
  static bool IsConnectionIdLengthValidForVersion(
      size_t connection_id_length,
      QuicTransportVersion transport_version);

  // Returns true if the connection ID is valid for this QUIC version.
  static bool IsConnectionIdValidForVersion(
      QuicConnectionId connection_id,
      QuicTransportVersion transport_version);

  // Returns a connection ID suitable for QUIC use-cases that do not need the
  // connection ID for multiplexing. If the version allows variable lengths,
  // a connection of length zero is returned, otherwise 64bits set to zero.
  static QuicConnectionId CreateZeroConnectionId(QuicTransportVersion version);

  // Generates a 128bit stateless reset token based on a connection ID.
  static QuicUint128 GenerateStatelessResetToken(
      QuicConnectionId connection_id);

  // Determines packet number space from |encryption_level|.
  static PacketNumberSpace GetPacketNumberSpace(
      EncryptionLevel encryption_level);

  // Determines encryption level to send packets in |packet_number_space|.
  static EncryptionLevel GetEncryptionLevel(
      PacketNumberSpace packet_number_space);

  // Get the maximum value for a V99/IETF QUIC stream count. If a count
  // exceeds this value, it will result in a stream ID that exceeds the
  // implementation limit on stream ID size.
  static QuicStreamCount GetMaxStreamCount();
};

template <typename Mask>
class QUIC_EXPORT_PRIVATE BitMask {
 public:
  // explicit to prevent (incorrect) usage like "BitMask bitmask = 0;".
  template <typename... Bits>
  explicit BitMask(Bits... bits) {
    mask_ = MakeMask(bits...);
  }

  BitMask() = default;
  BitMask(const BitMask& other) = default;
  BitMask& operator=(const BitMask& other) = default;

  template <typename... Bits>
  void Set(Bits... bits) {
    mask_ |= MakeMask(bits...);
  }

  template <typename Bit>
  bool IsSet(Bit bit) const {
    return (MakeMask(bit) & mask_) != 0;
  }

  void ClearAll() { mask_ = 0; }

  static constexpr size_t NumBits() { return 8 * sizeof(Mask); }

  friend bool operator==(const BitMask& lhs, const BitMask& rhs) {
    return lhs.mask_ == rhs.mask_;
  }

  std::string DebugString() const {
    std::ostringstream oss;
    oss << "0x" << std::hex << mask_;
    return oss.str();
  }

 private:
  template <typename Bit>
  static std::enable_if_t<std::is_enum<Bit>::value, Mask> MakeMask(Bit bit) {
    using IntType = typename std::underlying_type<Bit>::type;
    return Mask(1) << static_cast<IntType>(bit);
  }

  template <typename Bit>
  static std::enable_if_t<!std::is_enum<Bit>::value, Mask> MakeMask(Bit bit) {
    return Mask(1) << bit;
  }

  template <typename Bit, typename... Bits>
  static Mask MakeMask(Bit first_bit, Bits... other_bits) {
    return MakeMask(first_bit) | MakeMask(other_bits...);
  }

  Mask mask_ = 0;
};

using BitMask64 = BitMask<uint64_t>;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_UTILS_H_
