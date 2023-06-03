// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_utils.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#include "absl/base/macros.h"
#include "absl/base/optimization.h"
#include "absl/numeric/int128.h"
#include "absl/strings/string_view.h"
#include "openssl/sha.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_constants.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_endian.h"

namespace quic {
namespace {

// We know that >= GCC 4.8 and Clang have a __uint128_t intrinsic. Other
// compilers don't necessarily, notably MSVC.
#if defined(__x86_64__) &&                                         \
    ((defined(__GNUC__) &&                                         \
      (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8))) || \
     defined(__clang__))
#define QUIC_UTIL_HAS_UINT128 1
#endif

#ifdef QUIC_UTIL_HAS_UINT128
absl::uint128 IncrementalHashFast(absl::uint128 uhash, absl::string_view data) {
  // This code ends up faster than the naive implementation for 2 reasons:
  // 1. absl::uint128 is sufficiently complicated that the compiler
  //    cannot transform the multiplication by kPrime into a shift-multiply-add;
  //    it has go through all of the instructions for a 128-bit multiply.
  // 2. Because there are so fewer instructions (around 13), the hot loop fits
  //    nicely in the instruction queue of many Intel CPUs.
  // kPrime = 309485009821345068724781371
  static const absl::uint128 kPrime =
      (static_cast<absl::uint128>(16777216) << 64) + 315;
  auto hi = absl::Uint128High64(uhash);
  auto lo = absl::Uint128Low64(uhash);
  absl::uint128 xhash = (static_cast<absl::uint128>(hi) << 64) + lo;
  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t i = 0; i < data.length(); ++i) {
    xhash = (xhash ^ static_cast<uint32_t>(octets[i])) * kPrime;
  }
  return absl::MakeUint128(absl::Uint128High64(xhash),
                           absl::Uint128Low64(xhash));
}
#endif

#ifndef QUIC_UTIL_HAS_UINT128
// Slow implementation of IncrementalHash. In practice, only used by Chromium.
absl::uint128 IncrementalHashSlow(absl::uint128 hash, absl::string_view data) {
  // kPrime = 309485009821345068724781371
  static const absl::uint128 kPrime = absl::MakeUint128(16777216, 315);
  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t i = 0; i < data.length(); ++i) {
    hash = hash ^ absl::MakeUint128(0, octets[i]);
    hash = hash * kPrime;
  }
  return hash;
}
#endif

absl::uint128 IncrementalHash(absl::uint128 hash, absl::string_view data) {
#ifdef QUIC_UTIL_HAS_UINT128
  return IncrementalHashFast(hash, data);
#else
  return IncrementalHashSlow(hash, data);
#endif
}

}  // namespace

// static
uint64_t QuicUtils::FNV1a_64_Hash(absl::string_view data) {
  static const uint64_t kOffset = UINT64_C(14695981039346656037);
  static const uint64_t kPrime = UINT64_C(1099511628211);

  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data.data());

  uint64_t hash = kOffset;

  for (size_t i = 0; i < data.length(); ++i) {
    hash = hash ^ octets[i];
    hash = hash * kPrime;
  }

  return hash;
}

// static
absl::uint128 QuicUtils::FNV1a_128_Hash(absl::string_view data) {
  return FNV1a_128_Hash_Three(data, absl::string_view(), absl::string_view());
}

// static
absl::uint128 QuicUtils::FNV1a_128_Hash_Two(absl::string_view data1,
                                            absl::string_view data2) {
  return FNV1a_128_Hash_Three(data1, data2, absl::string_view());
}

// static
absl::uint128 QuicUtils::FNV1a_128_Hash_Three(absl::string_view data1,
                                              absl::string_view data2,
                                              absl::string_view data3) {
  // The two constants are defined as part of the hash algorithm.
  // see http://www.isthe.com/chongo/tech/comp/fnv/
  // kOffset = 144066263297769815596495629667062367629
  const absl::uint128 kOffset = absl::MakeUint128(
      UINT64_C(7809847782465536322), UINT64_C(7113472399480571277));

  absl::uint128 hash = IncrementalHash(kOffset, data1);
  if (data2.empty()) {
    return hash;
  }

  hash = IncrementalHash(hash, data2);
  if (data3.empty()) {
    return hash;
  }
  return IncrementalHash(hash, data3);
}

// static
void QuicUtils::SerializeUint128Short(absl::uint128 v, uint8_t* out) {
  const uint64_t lo = absl::Uint128Low64(v);
  const uint64_t hi = absl::Uint128High64(v);
  // This assumes that the system is little-endian.
  memcpy(out, &lo, sizeof(lo));
  memcpy(out + sizeof(lo), &hi, sizeof(hi) / 2);
}

#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x;

std::string QuicUtils::AddressChangeTypeToString(AddressChangeType type) {
  switch (type) {
    RETURN_STRING_LITERAL(NO_CHANGE);
    RETURN_STRING_LITERAL(PORT_CHANGE);
    RETURN_STRING_LITERAL(IPV4_SUBNET_CHANGE);
    RETURN_STRING_LITERAL(IPV4_TO_IPV6_CHANGE);
    RETURN_STRING_LITERAL(IPV6_TO_IPV4_CHANGE);
    RETURN_STRING_LITERAL(IPV6_TO_IPV6_CHANGE);
    RETURN_STRING_LITERAL(IPV4_TO_IPV4_CHANGE);
  }
  return "INVALID_ADDRESS_CHANGE_TYPE";
}

const char* QuicUtils::SentPacketStateToString(SentPacketState state) {
  switch (state) {
    RETURN_STRING_LITERAL(OUTSTANDING);
    RETURN_STRING_LITERAL(NEVER_SENT);
    RETURN_STRING_LITERAL(ACKED);
    RETURN_STRING_LITERAL(UNACKABLE);
    RETURN_STRING_LITERAL(NEUTERED);
    RETURN_STRING_LITERAL(HANDSHAKE_RETRANSMITTED);
    RETURN_STRING_LITERAL(LOST);
    RETURN_STRING_LITERAL(PTO_RETRANSMITTED);
    RETURN_STRING_LITERAL(NOT_CONTRIBUTING_RTT);
  }
  return "INVALID_SENT_PACKET_STATE";
}

// static
const char* QuicUtils::QuicLongHeaderTypetoString(QuicLongHeaderType type) {
  switch (type) {
    RETURN_STRING_LITERAL(VERSION_NEGOTIATION);
    RETURN_STRING_LITERAL(INITIAL);
    RETURN_STRING_LITERAL(RETRY);
    RETURN_STRING_LITERAL(HANDSHAKE);
    RETURN_STRING_LITERAL(ZERO_RTT_PROTECTED);
    default:
      return "INVALID_PACKET_TYPE";
  }
}

// static
const char* QuicUtils::AckResultToString(AckResult result) {
  switch (result) {
    RETURN_STRING_LITERAL(PACKETS_NEWLY_ACKED);
    RETURN_STRING_LITERAL(NO_PACKETS_NEWLY_ACKED);
    RETURN_STRING_LITERAL(UNSENT_PACKETS_ACKED);
    RETURN_STRING_LITERAL(UNACKABLE_PACKETS_ACKED);
    RETURN_STRING_LITERAL(PACKETS_ACKED_IN_WRONG_PACKET_NUMBER_SPACE);
  }
  return "INVALID_ACK_RESULT";
}

// static
AddressChangeType QuicUtils::DetermineAddressChangeType(
    const QuicSocketAddress& old_address,
    const QuicSocketAddress& new_address) {
  if (!old_address.IsInitialized() || !new_address.IsInitialized() ||
      old_address == new_address) {
    return NO_CHANGE;
  }

  if (old_address.host() == new_address.host()) {
    return PORT_CHANGE;
  }

  bool old_ip_is_ipv4 = old_address.host().IsIPv4() ? true : false;
  bool migrating_ip_is_ipv4 = new_address.host().IsIPv4() ? true : false;
  if (old_ip_is_ipv4 && !migrating_ip_is_ipv4) {
    return IPV4_TO_IPV6_CHANGE;
  }

  if (!old_ip_is_ipv4) {
    return migrating_ip_is_ipv4 ? IPV6_TO_IPV4_CHANGE : IPV6_TO_IPV6_CHANGE;
  }

  const int kSubnetMaskLength = 24;
  if (old_address.host().InSameSubnet(new_address.host(), kSubnetMaskLength)) {
    // Subnet part does not change (here, we use /24), which is considered to be
    // caused by NATs.
    return IPV4_SUBNET_CHANGE;
  }

  return IPV4_TO_IPV4_CHANGE;
}

// static
bool QuicUtils::IsAckable(SentPacketState state) {
  return state != NEVER_SENT && state != ACKED && state != UNACKABLE;
}

// static
bool QuicUtils::IsRetransmittableFrame(QuicFrameType type) {
  switch (type) {
    case ACK_FRAME:
    case PADDING_FRAME:
    case STOP_WAITING_FRAME:
    case MTU_DISCOVERY_FRAME:
    case PATH_CHALLENGE_FRAME:
    case PATH_RESPONSE_FRAME:
      return false;
    default:
      return true;
  }
}

// static
bool QuicUtils::IsHandshakeFrame(const QuicFrame& frame,
                                 QuicTransportVersion transport_version) {
  if (!QuicVersionUsesCryptoFrames(transport_version)) {
    return frame.type == STREAM_FRAME &&
           frame.stream_frame.stream_id == GetCryptoStreamId(transport_version);
  } else {
    return frame.type == CRYPTO_FRAME;
  }
}

// static
bool QuicUtils::ContainsFrameType(const QuicFrames& frames,
                                  QuicFrameType type) {
  for (const QuicFrame& frame : frames) {
    if (frame.type == type) {
      return true;
    }
  }
  return false;
}

// static
SentPacketState QuicUtils::RetransmissionTypeToPacketState(
    TransmissionType retransmission_type) {
  switch (retransmission_type) {
    case ALL_ZERO_RTT_RETRANSMISSION:
      return UNACKABLE;
    case HANDSHAKE_RETRANSMISSION:
      return HANDSHAKE_RETRANSMITTED;
    case LOSS_RETRANSMISSION:
      return LOST;
    case PTO_RETRANSMISSION:
      return PTO_RETRANSMITTED;
    case PATH_RETRANSMISSION:
      return NOT_CONTRIBUTING_RTT;
    case ALL_INITIAL_RETRANSMISSION:
      return UNACKABLE;
    default:
      QUIC_BUG(quic_bug_10839_2)
          << retransmission_type << " is not a retransmission_type";
      return UNACKABLE;
  }
}

// static
bool QuicUtils::IsIetfPacketHeader(uint8_t first_byte) {
  return (first_byte & FLAGS_LONG_HEADER) || (first_byte & FLAGS_FIXED_BIT) ||
         !(first_byte & FLAGS_DEMULTIPLEXING_BIT);
}

// static
bool QuicUtils::IsIetfPacketShortHeader(uint8_t first_byte) {
  return IsIetfPacketHeader(first_byte) && !(first_byte & FLAGS_LONG_HEADER);
}

// static
QuicStreamId QuicUtils::GetInvalidStreamId(QuicTransportVersion version) {
  return VersionHasIetfQuicFrames(version)
             ? std::numeric_limits<QuicStreamId>::max()
             : 0;
}

// static
QuicStreamId QuicUtils::GetCryptoStreamId(QuicTransportVersion version) {
  QUIC_BUG_IF(quic_bug_12982_1, QuicVersionUsesCryptoFrames(version))
      << "CRYPTO data aren't in stream frames; they have no stream ID.";
  return QuicVersionUsesCryptoFrames(version) ? GetInvalidStreamId(version) : 1;
}

// static
bool QuicUtils::IsCryptoStreamId(QuicTransportVersion version,
                                 QuicStreamId stream_id) {
  if (QuicVersionUsesCryptoFrames(version)) {
    return false;
  }
  return stream_id == GetCryptoStreamId(version);
}

// static
QuicStreamId QuicUtils::GetHeadersStreamId(QuicTransportVersion version) {
  QUICHE_DCHECK(!VersionUsesHttp3(version));
  return GetFirstBidirectionalStreamId(version, Perspective::IS_CLIENT);
}

// static
bool QuicUtils::IsClientInitiatedStreamId(QuicTransportVersion version,
                                          QuicStreamId id) {
  if (id == GetInvalidStreamId(version)) {
    return false;
  }
  return VersionHasIetfQuicFrames(version) ? id % 2 == 0 : id % 2 != 0;
}

// static
bool QuicUtils::IsServerInitiatedStreamId(QuicTransportVersion version,
                                          QuicStreamId id) {
  if (id == GetInvalidStreamId(version)) {
    return false;
  }
  return VersionHasIetfQuicFrames(version) ? id % 2 != 0 : id % 2 == 0;
}

// static
bool QuicUtils::IsOutgoingStreamId(ParsedQuicVersion version, QuicStreamId id,
                                   Perspective perspective) {
  // Streams are outgoing streams, iff:
  // - we are the server and the stream is server-initiated
  // - we are the client and the stream is client-initiated.
  const bool perspective_is_server = perspective == Perspective::IS_SERVER;
  const bool stream_is_server =
      QuicUtils::IsServerInitiatedStreamId(version.transport_version, id);
  return perspective_is_server == stream_is_server;
}

// static
bool QuicUtils::IsBidirectionalStreamId(QuicStreamId id,
                                        ParsedQuicVersion version) {
  QUICHE_DCHECK(version.HasIetfQuicFrames());
  return id % 4 < 2;
}

// static
StreamType QuicUtils::GetStreamType(QuicStreamId id, Perspective perspective,
                                    bool peer_initiated,
                                    ParsedQuicVersion version) {
  QUICHE_DCHECK(version.HasIetfQuicFrames());
  if (IsBidirectionalStreamId(id, version)) {
    return BIDIRECTIONAL;
  }

  if (peer_initiated) {
    if (perspective == Perspective::IS_SERVER) {
      QUICHE_DCHECK_EQ(2u, id % 4);
    } else {
      QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective);
      QUICHE_DCHECK_EQ(3u, id % 4);
    }
    return READ_UNIDIRECTIONAL;
  }

  if (perspective == Perspective::IS_SERVER) {
    QUICHE_DCHECK_EQ(3u, id % 4);
  } else {
    QUICHE_DCHECK_EQ(Perspective::IS_CLIENT, perspective);
    QUICHE_DCHECK_EQ(2u, id % 4);
  }
  return WRITE_UNIDIRECTIONAL;
}

// static
QuicStreamId QuicUtils::StreamIdDelta(QuicTransportVersion version) {
  return VersionHasIetfQuicFrames(version) ? 4 : 2;
}

// static
QuicStreamId QuicUtils::GetFirstBidirectionalStreamId(
    QuicTransportVersion version, Perspective perspective) {
  if (VersionHasIetfQuicFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 0 : 1;
  } else if (QuicVersionUsesCryptoFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 1 : 2;
  }
  return perspective == Perspective::IS_CLIENT ? 3 : 2;
}

// static
QuicStreamId QuicUtils::GetFirstUnidirectionalStreamId(
    QuicTransportVersion version, Perspective perspective) {
  if (VersionHasIetfQuicFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 2 : 3;
  } else if (QuicVersionUsesCryptoFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 1 : 2;
  }
  return perspective == Perspective::IS_CLIENT ? 3 : 2;
}

// static
QuicStreamId QuicUtils::GetMaxClientInitiatedBidirectionalStreamId(
    QuicTransportVersion version) {
  if (VersionHasIetfQuicFrames(version)) {
    // Client initiated bidirectional streams have stream IDs divisible by 4.
    return std::numeric_limits<QuicStreamId>::max() - 3;
  }

  // Client initiated bidirectional streams have odd stream IDs.
  return std::numeric_limits<QuicStreamId>::max();
}

// static
QuicConnectionId QuicUtils::CreateRandomConnectionId() {
  return CreateRandomConnectionId(kQuicDefaultConnectionIdLength,
                                  QuicRandom::GetInstance());
}

// static
QuicConnectionId QuicUtils::CreateRandomConnectionId(QuicRandom* random) {
  return CreateRandomConnectionId(kQuicDefaultConnectionIdLength, random);
}
// static
QuicConnectionId QuicUtils::CreateRandomConnectionId(
    uint8_t connection_id_length) {
  return CreateRandomConnectionId(connection_id_length,
                                  QuicRandom::GetInstance());
}

// static
QuicConnectionId QuicUtils::CreateRandomConnectionId(
    uint8_t connection_id_length, QuicRandom* random) {
  QuicConnectionId connection_id;
  connection_id.set_length(connection_id_length);
  if (connection_id.length() > 0) {
    random->RandBytes(connection_id.mutable_data(), connection_id.length());
  }
  return connection_id;
}

// static
QuicConnectionId QuicUtils::CreateZeroConnectionId(
    QuicTransportVersion version) {
  if (!VersionAllowsVariableLengthConnectionIds(version)) {
    char connection_id_bytes[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    return QuicConnectionId(static_cast<char*>(connection_id_bytes),
                            ABSL_ARRAYSIZE(connection_id_bytes));
  }
  return EmptyQuicConnectionId();
}

// static
bool QuicUtils::IsConnectionIdLengthValidForVersion(
    size_t connection_id_length, QuicTransportVersion transport_version) {
  // No version of QUIC can support lengths that do not fit in an uint8_t.
  if (connection_id_length >
      static_cast<size_t>(std::numeric_limits<uint8_t>::max())) {
    return false;
  }

  if (transport_version == QUIC_VERSION_UNSUPPORTED ||
      transport_version == QUIC_VERSION_RESERVED_FOR_NEGOTIATION) {
    // Unknown versions could allow connection ID lengths up to 255.
    return true;
  }

  const uint8_t connection_id_length8 =
      static_cast<uint8_t>(connection_id_length);
  // Versions that do not support variable lengths only support length 8.
  if (!VersionAllowsVariableLengthConnectionIds(transport_version)) {
    return connection_id_length8 == kQuicDefaultConnectionIdLength;
  }
  return connection_id_length8 <= kQuicMaxConnectionIdWithLengthPrefixLength;
}

// static
bool QuicUtils::IsConnectionIdValidForVersion(
    QuicConnectionId connection_id, QuicTransportVersion transport_version) {
  return IsConnectionIdLengthValidForVersion(connection_id.length(),
                                             transport_version);
}

StatelessResetToken QuicUtils::GenerateStatelessResetToken(
    QuicConnectionId connection_id) {
  static_assert(sizeof(absl::uint128) == sizeof(StatelessResetToken),
                "bad size");
  static_assert(alignof(absl::uint128) >= alignof(StatelessResetToken),
                "bad alignment");
  absl::uint128 hash = FNV1a_128_Hash(
      absl::string_view(connection_id.data(), connection_id.length()));
  return *reinterpret_cast<StatelessResetToken*>(&hash);
}

// static
QuicStreamCount QuicUtils::GetMaxStreamCount() {
  return (kMaxQuicStreamCount >> 2) + 1;
}

// static
PacketNumberSpace QuicUtils::GetPacketNumberSpace(
    EncryptionLevel encryption_level) {
  switch (encryption_level) {
    case ENCRYPTION_INITIAL:
      return INITIAL_DATA;
    case ENCRYPTION_HANDSHAKE:
      return HANDSHAKE_DATA;
    case ENCRYPTION_ZERO_RTT:
    case ENCRYPTION_FORWARD_SECURE:
      return APPLICATION_DATA;
    default:
      QUIC_BUG(quic_bug_10839_3)
          << "Try to get packet number space of encryption level: "
          << encryption_level;
      return NUM_PACKET_NUMBER_SPACES;
  }
}

// static
EncryptionLevel QuicUtils::GetEncryptionLevelToSendAckofSpace(
    PacketNumberSpace packet_number_space) {
  switch (packet_number_space) {
    case INITIAL_DATA:
      return ENCRYPTION_INITIAL;
    case HANDSHAKE_DATA:
      return ENCRYPTION_HANDSHAKE;
    case APPLICATION_DATA:
      return ENCRYPTION_FORWARD_SECURE;
    default:
      QUICHE_DCHECK(false);
      return NUM_ENCRYPTION_LEVELS;
  }
}

// static
bool QuicUtils::IsProbingFrame(QuicFrameType type) {
  switch (type) {
    case PATH_CHALLENGE_FRAME:
    case PATH_RESPONSE_FRAME:
    case NEW_CONNECTION_ID_FRAME:
    case PADDING_FRAME:
      return true;
    default:
      return false;
  }
}

// static
bool QuicUtils::IsAckElicitingFrame(QuicFrameType type) {
  switch (type) {
    case PADDING_FRAME:
    case STOP_WAITING_FRAME:
    case ACK_FRAME:
    case CONNECTION_CLOSE_FRAME:
      return false;
    default:
      return true;
  }
}

// static
bool QuicUtils::AreStatelessResetTokensEqual(
    const StatelessResetToken& token1, const StatelessResetToken& token2) {
  char byte = 0;
  for (size_t i = 0; i < kStatelessResetTokenLength; i++) {
    // This avoids compiler optimizations that could make us stop comparing
    // after we find a byte that doesn't match.
    byte |= (token1[i] ^ token2[i]);
  }
  return byte == 0;
}

bool IsValidWebTransportSessionId(WebTransportSessionId id,
                                  ParsedQuicVersion version) {
  QUICHE_DCHECK(version.UsesHttp3());
  return (id <= std::numeric_limits<QuicStreamId>::max()) &&
         QuicUtils::IsBidirectionalStreamId(id, version) &&
         QuicUtils::IsClientInitiatedStreamId(version.transport_version, id);
}

QuicByteCount MemSliceSpanTotalSize(absl::Span<quiche::QuicheMemSlice> span) {
  QuicByteCount total = 0;
  for (const quiche::QuicheMemSlice& slice : span) {
    total += slice.length();
  }
  return total;
}

std::string RawSha256(absl::string_view input) {
  std::string raw_hash;
  raw_hash.resize(SHA256_DIGEST_LENGTH);
  SHA256(reinterpret_cast<const uint8_t*>(input.data()), input.size(),
         reinterpret_cast<uint8_t*>(&raw_hash[0]));
  return raw_hash;
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds
}  // namespace quic
