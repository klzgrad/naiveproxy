// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_utils.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_aligned.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_prefetch.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_uint128.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

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
QuicUint128 IncrementalHashFast(QuicUint128 uhash,
                                quiche::QuicheStringPiece data) {
  // This code ends up faster than the naive implementation for 2 reasons:
  // 1. QuicUint128 is sufficiently complicated that the compiler
  //    cannot transform the multiplication by kPrime into a shift-multiply-add;
  //    it has go through all of the instructions for a 128-bit multiply.
  // 2. Because there are so fewer instructions (around 13), the hot loop fits
  //    nicely in the instruction queue of many Intel CPUs.
  // kPrime = 309485009821345068724781371
  static const QuicUint128 kPrime =
      (static_cast<QuicUint128>(16777216) << 64) + 315;
  auto hi = QuicUint128High64(uhash);
  auto lo = QuicUint128Low64(uhash);
  QuicUint128 xhash = (static_cast<QuicUint128>(hi) << 64) + lo;
  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t i = 0; i < data.length(); ++i) {
    xhash = (xhash ^ static_cast<uint32_t>(octets[i])) * kPrime;
  }
  return MakeQuicUint128(QuicUint128High64(xhash), QuicUint128Low64(xhash));
}
#endif

#ifndef QUIC_UTIL_HAS_UINT128
// Slow implementation of IncrementalHash. In practice, only used by Chromium.
QuicUint128 IncrementalHashSlow(QuicUint128 hash,
                                quiche::QuicheStringPiece data) {
  // kPrime = 309485009821345068724781371
  static const QuicUint128 kPrime = MakeQuicUint128(16777216, 315);
  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t i = 0; i < data.length(); ++i) {
    hash = hash ^ MakeQuicUint128(0, octets[i]);
    hash = hash * kPrime;
  }
  return hash;
}
#endif

QuicUint128 IncrementalHash(QuicUint128 hash, quiche::QuicheStringPiece data) {
#ifdef QUIC_UTIL_HAS_UINT128
  return IncrementalHashFast(hash, data);
#else
  return IncrementalHashSlow(hash, data);
#endif
}

}  // namespace

// static
uint64_t QuicUtils::FNV1a_64_Hash(quiche::QuicheStringPiece data) {
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
QuicUint128 QuicUtils::FNV1a_128_Hash(quiche::QuicheStringPiece data) {
  return FNV1a_128_Hash_Three(data, quiche::QuicheStringPiece(),
                              quiche::QuicheStringPiece());
}

// static
QuicUint128 QuicUtils::FNV1a_128_Hash_Two(quiche::QuicheStringPiece data1,
                                          quiche::QuicheStringPiece data2) {
  return FNV1a_128_Hash_Three(data1, data2, quiche::QuicheStringPiece());
}

// static
QuicUint128 QuicUtils::FNV1a_128_Hash_Three(quiche::QuicheStringPiece data1,
                                            quiche::QuicheStringPiece data2,
                                            quiche::QuicheStringPiece data3) {
  // The two constants are defined as part of the hash algorithm.
  // see http://www.isthe.com/chongo/tech/comp/fnv/
  // kOffset = 144066263297769815596495629667062367629
  const QuicUint128 kOffset = MakeQuicUint128(UINT64_C(7809847782465536322),
                                              UINT64_C(7113472399480571277));

  QuicUint128 hash = IncrementalHash(kOffset, data1);
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
void QuicUtils::SerializeUint128Short(QuicUint128 v, uint8_t* out) {
  const uint64_t lo = QuicUint128Low64(v);
  const uint64_t hi = QuicUint128High64(v);
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
    RETURN_STRING_LITERAL(TLP_RETRANSMITTED);
    RETURN_STRING_LITERAL(RTO_RETRANSMITTED);
    RETURN_STRING_LITERAL(PTO_RETRANSMITTED);
    RETURN_STRING_LITERAL(PROBE_RETRANSMITTED);
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
void QuicUtils::CopyToBuffer(const struct iovec* iov,
                             int iov_count,
                             size_t iov_offset,
                             size_t buffer_length,
                             char* buffer) {
  int iovnum = 0;
  while (iovnum < iov_count && iov_offset >= iov[iovnum].iov_len) {
    iov_offset -= iov[iovnum].iov_len;
    ++iovnum;
  }
  DCHECK_LE(iovnum, iov_count);
  DCHECK_LE(iov_offset, iov[iovnum].iov_len);
  if (iovnum >= iov_count || buffer_length == 0) {
    return;
  }

  // Unroll the first iteration that handles iov_offset.
  const size_t iov_available = iov[iovnum].iov_len - iov_offset;
  size_t copy_len = std::min(buffer_length, iov_available);

  // Try to prefetch the next iov if there is at least one more after the
  // current. Otherwise, it looks like an irregular access that the hardware
  // prefetcher won't speculatively prefetch. Only prefetch one iov because
  // generally, the iov_offset is not 0, input iov consists of 2K buffers and
  // the output buffer is ~1.4K.
  if (copy_len == iov_available && iovnum + 1 < iov_count) {
    char* next_base = static_cast<char*>(iov[iovnum + 1].iov_base);
    // Prefetch 2 cachelines worth of data to get the prefetcher started; leave
    // it to the hardware prefetcher after that.
    QuicPrefetchT0(next_base);
    if (iov[iovnum + 1].iov_len >= 64) {
      QuicPrefetchT0(next_base + QUIC_CACHELINE_SIZE);
    }
  }

  const char* src = static_cast<char*>(iov[iovnum].iov_base) + iov_offset;
  while (true) {
    memcpy(buffer, src, copy_len);
    buffer_length -= copy_len;
    buffer += copy_len;
    if (buffer_length == 0 || ++iovnum >= iov_count) {
      break;
    }
    src = static_cast<char*>(iov[iovnum].iov_base);
    copy_len = std::min(buffer_length, iov[iovnum].iov_len);
  }
  QUIC_BUG_IF(buffer_length > 0) << "Failed to copy entire length to buffer.";
}

// static
struct iovec QuicUtils::MakeIovec(quiche::QuicheStringPiece data) {
  struct iovec iov = {const_cast<char*>(data.data()),
                      static_cast<size_t>(data.size())};
  return iov;
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
    case ALL_UNACKED_RETRANSMISSION:
    case ALL_INITIAL_RETRANSMISSION:
      return UNACKABLE;
    case HANDSHAKE_RETRANSMISSION:
      return HANDSHAKE_RETRANSMITTED;
    case LOSS_RETRANSMISSION:
      return LOST;
    case TLP_RETRANSMISSION:
      return TLP_RETRANSMITTED;
    case RTO_RETRANSMISSION:
      return RTO_RETRANSMITTED;
    case PTO_RETRANSMISSION:
      return PTO_RETRANSMITTED;
    case PROBING_RETRANSMISSION:
      return PROBE_RETRANSMITTED;
    default:
      QUIC_BUG << TransmissionTypeToString(retransmission_type)
               << " is not a retransmission_type";
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
  QUIC_BUG_IF(QuicVersionUsesCryptoFrames(version))
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
  DCHECK(!VersionUsesHttp3(version));
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
bool QuicUtils::IsOutgoingStreamId(ParsedQuicVersion version,
                                   QuicStreamId id,
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
bool QuicUtils::IsBidirectionalStreamId(QuicStreamId id) {
  return id % 4 < 2;
}

// static
StreamType QuicUtils::GetStreamType(QuicStreamId id,
                                    Perspective perspective,
                                    bool peer_initiated) {
  if (IsBidirectionalStreamId(id)) {
    return BIDIRECTIONAL;
  }

  if (peer_initiated) {
    if (perspective == Perspective::IS_SERVER) {
      DCHECK_EQ(2u, id % 4);
    } else {
      DCHECK_EQ(Perspective::IS_CLIENT, perspective);
      DCHECK_EQ(3u, id % 4);
    }
    return READ_UNIDIRECTIONAL;
  }

  if (perspective == Perspective::IS_SERVER) {
    DCHECK_EQ(3u, id % 4);
  } else {
    DCHECK_EQ(Perspective::IS_CLIENT, perspective);
    DCHECK_EQ(2u, id % 4);
  }
  return WRITE_UNIDIRECTIONAL;
}

// static
QuicStreamId QuicUtils::StreamIdDelta(QuicTransportVersion version) {
  return VersionHasIetfQuicFrames(version) ? 4 : 2;
}

// static
QuicStreamId QuicUtils::GetFirstBidirectionalStreamId(
    QuicTransportVersion version,
    Perspective perspective) {
  if (VersionHasIetfQuicFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 0 : 1;
  } else if (QuicVersionUsesCryptoFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 1 : 2;
  }
  return perspective == Perspective::IS_CLIENT ? 3 : 2;
}

// static
QuicStreamId QuicUtils::GetFirstUnidirectionalStreamId(
    QuicTransportVersion version,
    Perspective perspective) {
  if (VersionHasIetfQuicFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 2 : 3;
  } else if (QuicVersionUsesCryptoFrames(version)) {
    return perspective == Perspective::IS_CLIENT ? 1 : 2;
  }
  return perspective == Perspective::IS_CLIENT ? 3 : 2;
}

// static
QuicConnectionId QuicUtils::CreateReplacementConnectionId(
    QuicConnectionId connection_id) {
  const uint64_t connection_id_hash = FNV1a_64_Hash(
      quiche::QuicheStringPiece(connection_id.data(), connection_id.length()));
  return QuicConnectionId(reinterpret_cast<const char*>(&connection_id_hash),
                          sizeof(connection_id_hash));
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
    uint8_t connection_id_length,
    QuicRandom* random) {
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
                            QUICHE_ARRAYSIZE(connection_id_bytes));
  }
  return EmptyQuicConnectionId();
}

// static
bool QuicUtils::IsConnectionIdLengthValidForVersion(
    size_t connection_id_length,
    QuicTransportVersion transport_version) {
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
  // Versions that do support variable length but do not have length-prefixed
  // connection IDs use the 4-bit connection ID length encoding which can
  // only encode values 0 and 4-18.
  if (!VersionHasLengthPrefixedConnectionIds(transport_version)) {
    return connection_id_length8 == 0 ||
           (connection_id_length8 >= 4 &&
            connection_id_length8 <= kQuicMaxConnectionId4BitLength);
  }
  return connection_id_length8 <= kQuicMaxConnectionIdWithLengthPrefixLength;
}

// static
bool QuicUtils::IsConnectionIdValidForVersion(
    QuicConnectionId connection_id,
    QuicTransportVersion transport_version) {
  return IsConnectionIdLengthValidForVersion(connection_id.length(),
                                             transport_version);
}

QuicUint128 QuicUtils::GenerateStatelessResetToken(
    QuicConnectionId connection_id) {
  return FNV1a_128_Hash(
      quiche::QuicheStringPiece(connection_id.data(), connection_id.length()));
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
      QUIC_BUG << "Try to get packet number space of encryption level: "
               << EncryptionLevelToString(encryption_level);
      return NUM_PACKET_NUMBER_SPACES;
  }
}

// static
EncryptionLevel QuicUtils::GetEncryptionLevel(
    PacketNumberSpace packet_number_space) {
  switch (packet_number_space) {
    case INITIAL_DATA:
      return ENCRYPTION_INITIAL;
    case HANDSHAKE_DATA:
      return ENCRYPTION_HANDSHAKE;
    case APPLICATION_DATA:
      return ENCRYPTION_FORWARD_SECURE;
    default:
      DCHECK(false);
      return NUM_ENCRYPTION_LEVELS;
  }
}

#undef RETURN_STRING_LITERAL  // undef for jumbo builds
}  // namespace quic
