// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_UTILS_H_
#define QUICHE_QUIC_CORE_QUIC_UTILS_H_

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <type_traits>

#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace quic {

class QUICHE_EXPORT QuicUtils {
 public:
  QuicUtils() = delete;

  // Returns the 64 bit FNV1a hash of the data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static uint64_t FNV1a_64_Hash(absl::string_view data);

  // Returns the 128 bit FNV1a hash of the data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static absl::uint128 FNV1a_128_Hash(absl::string_view data);

  // Returns the 128 bit FNV1a hash of the two sequences of data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static absl::uint128 FNV1a_128_Hash_Two(absl::string_view data1,
                                          absl::string_view data2);

  // Returns the 128 bit FNV1a hash of the three sequences of data.  See
  // http://www.isthe.com/chongo/tech/comp/fnv/index.html#FNV-param
  static absl::uint128 FNV1a_128_Hash_Three(absl::string_view data1,
                                            absl::string_view data2,
                                            absl::string_view data3);

  // SerializeUint128 writes the first 96 bits of |v| in little-endian form
  // to |out|.
  static void SerializeUint128Short(absl::uint128 v, uint8_t* out);

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
  static bool IsOutgoingStreamId(ParsedQuicVersion version, QuicStreamId id,
                                 Perspective perspective);

  // Returns true if |id| is considered as bidirectional stream ID. Only used in
  // v99.
  static bool IsBidirectionalStreamId(QuicStreamId id,
                                      ParsedQuicVersion version);

  // Returns stream type.  Either |perspective| or |peer_initiated| would be
  // enough together with |id|.  This method enforces that the three parameters
  // are consistent.  Only used in v99.
  static StreamType GetStreamType(QuicStreamId id, Perspective perspective,
                                  bool peer_initiated,
                                  ParsedQuicVersion version);

  // Returns the delta between consecutive stream IDs of the same type.
  static QuicStreamId StreamIdDelta(QuicTransportVersion version);

  // Returns the first initiated bidirectional stream ID of |perspective|.
  static QuicStreamId GetFirstBidirectionalStreamId(
      QuicTransportVersion version, Perspective perspective);

  // Returns the first initiated unidirectional stream ID of |perspective|.
  static QuicStreamId GetFirstUnidirectionalStreamId(
      QuicTransportVersion version, Perspective perspective);

  // Returns the largest possible client initiated bidirectional stream ID.
  static QuicStreamId GetMaxClientInitiatedBidirectionalStreamId(
      QuicTransportVersion version);

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
      size_t connection_id_length, QuicTransportVersion transport_version);

  // Returns true if the connection ID is valid for this QUIC version.
  static bool IsConnectionIdValidForVersion(
      QuicConnectionId connection_id, QuicTransportVersion transport_version);

  // Returns a connection ID suitable for QUIC use-cases that do not need the
  // connection ID for multiplexing. If the version allows variable lengths,
  // a connection of length zero is returned, otherwise 64bits set to zero.
  static QuicConnectionId CreateZeroConnectionId(QuicTransportVersion version);

  // Generates a 128bit stateless reset token based on a connection ID.
  static StatelessResetToken GenerateStatelessResetToken(
      QuicConnectionId connection_id);

  // Determines packet number space from |encryption_level|.
  static PacketNumberSpace GetPacketNumberSpace(
      EncryptionLevel encryption_level);

  // Determines encryption level to send ACK in |packet_number_space|.
  static EncryptionLevel GetEncryptionLevelToSendAckofSpace(
      PacketNumberSpace packet_number_space);

  // Get the maximum value for a V99/IETF QUIC stream count. If a count
  // exceeds this value, it will result in a stream ID that exceeds the
  // implementation limit on stream ID size.
  static QuicStreamCount GetMaxStreamCount();

  // Return true if this frame is an IETF probing frame.
  static bool IsProbingFrame(QuicFrameType type);

  // Return true if the two stateless reset tokens are equal. Performs the
  // comparison in constant time.
  static bool AreStatelessResetTokensEqual(const StatelessResetToken& token1,
                                           const StatelessResetToken& token2);

  // Return ture if this frame is an ack-eliciting frame.
  static bool IsAckElicitingFrame(QuicFrameType type);
};

// Returns true if the specific ID is a valid WebTransport session ID that our
// implementation can process.
bool IsValidWebTransportSessionId(WebTransportSessionId id,
                                  ParsedQuicVersion transport_version);

QuicByteCount MemSliceSpanTotalSize(absl::Span<quiche::QuicheMemSlice> span);

// Returns the part of the path after the final "/".  If there is no "/" in the
// path, the result is the same as the input.
// Note that this function's behavior differs from the POSIX standard basename
// function if path ends with "/". For such paths, this function returns the
// empty string.
// This function returns path as-is, if it's windows path with backslash
// separators.
QUICHE_EXPORT absl::string_view PosixBasename(absl::string_view path);

// Computes a SHA-256 hash and returns the raw bytes of the hash.
QUICHE_EXPORT std::string RawSha256(absl::string_view input);

// BitMask<Index, Mask> is a set of elements of type `Index` represented as a
// bitmask of an underlying integer type `Mask` (uint64_t by default). The
// underlying type has to be large enough to fit all possible values of `Index`.
template <typename Index, typename Mask = uint64_t>
class QUICHE_EXPORT BitMask {
 public:
  explicit constexpr BitMask(std::initializer_list<Index> bits) {
    for (Index bit : bits) {
      mask_ |= MakeMask(bit);
    }
  }

  BitMask() = default;
  BitMask(const BitMask& other) = default;
  BitMask& operator=(const BitMask& other) = default;

  constexpr void Set(Index bit) { mask_ |= MakeMask(bit); }

  constexpr void Set(std::initializer_list<Index> bits) {
    mask_ |= BitMask(bits).mask();
  }

  constexpr bool IsSet(Index bit) const { return (MakeMask(bit) & mask_) != 0; }

  constexpr void ClearAll() { mask_ = 0; }

  // Returns true if any of the bits is set.
  bool Any() const { return mask_ != 0; }

  // Returns the highest bit set, or nullopt if the mask is all zeroes.
  std::optional<Index> Max() const {
    if (!Any()) {
      return std::nullopt;
    }
    return static_cast<Index>(NumBits() - absl::countl_zero(mask_) - 1);
  }

  static constexpr size_t NumBits() { return 8 * sizeof(Mask); }

  friend bool operator==(const BitMask& lhs, const BitMask& rhs) {
    return lhs.mask_ == rhs.mask_;
  }

  // Bitwise AND that can act as a set intersection between two bit masks.
  BitMask<Index, Mask> operator&(const BitMask<Index, Mask>& rhs) const {
    return BitMask<Index, Mask>(mask_ & rhs.mask_);
  }

  std::string DebugString() const {
    return absl::StrCat("0x", absl::Hex(mask_));
  }

  constexpr Mask mask() const { return mask_; }

 private:
  explicit constexpr BitMask(Mask mask) : mask_(mask) {}

  template <typename Bit>
  static constexpr std::enable_if_t<std::is_enum_v<Bit>, Mask> MakeMask(
      Bit bit) {
    using IntType = typename std::underlying_type<Bit>::type;
    return MakeMask(static_cast<IntType>(bit));
  }

  template <typename Bit>
  static constexpr std::enable_if_t<!std::is_enum_v<Bit>, Mask> MakeMask(
      Bit bit) {
    // We can't use QUICHE_DCHECK_LT here, since it doesn't work with constexpr.
    QUICHE_DCHECK(bit < static_cast<Bit>(NumBits()));
    if constexpr (std::is_signed_v<Bit>) {
      QUICHE_DCHECK(bit >= 0);
    }
    return Mask(1) << bit;
  }

  Mask mask_ = 0;
};

// Ensure that the BitMask constructor can be evaluated as constexpr.
static_assert(BitMask<int>({1, 2, 3}).mask() == 0x0e);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_UTILS_H_
