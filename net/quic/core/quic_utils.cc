// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_utils.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "base/containers/adapters.h"
#include "base/logging.h"
#include "net/quic/core/quic_constants.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_flags.h"

using std::string;

namespace net {
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
uint128 IncrementalHashFast(uint128 uhash, QuicStringPiece data) {
  // This code ends up faster than the naive implementation for 2 reasons:
  // 1. uint128 from base/int128.h is sufficiently complicated that the compiler
  //    cannot transform the multiplication by kPrime into a shift-multiply-add;
  //    it has go through all of the instructions for a 128-bit multiply.
  // 2. Because there are so fewer instructions (around 13), the hot loop fits
  //    nicely in the instruction queue of many Intel CPUs.
  // kPrime = 309485009821345068724781371
  static const __uint128_t kPrime =
      (static_cast<__uint128_t>(16777216) << 64) + 315;
  __uint128_t xhash = (static_cast<__uint128_t>(Uint128High64(uhash)) << 64) +
                      Uint128Low64(uhash);
  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t i = 0; i < data.length(); ++i) {
    xhash = (xhash ^ octets[i]) * kPrime;
  }
  return MakeUint128(
      static_cast<uint64_t>(xhash >> 64),
      static_cast<uint64_t>(xhash & UINT64_C(0xFFFFFFFFFFFFFFFF)));
}
#endif

#ifndef QUIC_UTIL_HAS_UINT128
// Slow implementation of IncrementalHash. In practice, only used by Chromium.
uint128 IncrementalHashSlow(uint128 hash, QuicStringPiece data) {
  // kPrime = 309485009821345068724781371
  static const uint128 kPrime = MakeUint128(16777216, 315);
  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data.data());
  for (size_t i = 0; i < data.length(); ++i) {
    hash = hash ^ MakeUint128(0, octets[i]);
    hash = hash * kPrime;
  }
  return hash;
}
#endif

uint128 IncrementalHash(uint128 hash, QuicStringPiece data) {
#ifdef QUIC_UTIL_HAS_UINT128
  return IncrementalHashFast(hash, data);
#else
  return IncrementalHashSlow(hash, data);
#endif
}

}  // namespace

// static
uint64_t QuicUtils::FNV1a_64_Hash(QuicStringPiece data) {
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
uint128 QuicUtils::FNV1a_128_Hash(QuicStringPiece data) {
  return FNV1a_128_Hash_Three(data, QuicStringPiece(), QuicStringPiece());
}

// static
uint128 QuicUtils::FNV1a_128_Hash_Two(QuicStringPiece data1,
                                      QuicStringPiece data2) {
  return FNV1a_128_Hash_Three(data1, data2, QuicStringPiece());
}

// static
uint128 QuicUtils::FNV1a_128_Hash_Three(QuicStringPiece data1,
                                        QuicStringPiece data2,
                                        QuicStringPiece data3) {
  // The two constants are defined as part of the hash algorithm.
  // see http://www.isthe.com/chongo/tech/comp/fnv/
  // kOffset = 144066263297769815596495629667062367629
  const uint128 kOffset =
      MakeUint128(UINT64_C(7809847782465536322), UINT64_C(7113472399480571277));

  uint128 hash = IncrementalHash(kOffset, data1);
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
void QuicUtils::SerializeUint128Short(uint128 v, uint8_t* out) {
  const uint64_t lo = Uint128Low64(v);
  const uint64_t hi = Uint128High64(v);
  // This assumes that the system is little-endian.
  memcpy(out, &lo, sizeof(lo));
  memcpy(out + sizeof(lo), &hi, sizeof(hi) / 2);
}

#define RETURN_STRING_LITERAL(x) \
  case x:                        \
    return #x;

// static
const char* QuicUtils::EncryptionLevelToString(EncryptionLevel level) {
  switch (level) {
    RETURN_STRING_LITERAL(ENCRYPTION_NONE);
    RETURN_STRING_LITERAL(ENCRYPTION_INITIAL);
    RETURN_STRING_LITERAL(ENCRYPTION_FORWARD_SECURE);
    RETURN_STRING_LITERAL(NUM_ENCRYPTION_LEVELS);
  }
  return "INVALID_ENCRYPTION_LEVEL";
}

// static
const char* QuicUtils::TransmissionTypeToString(TransmissionType type) {
  switch (type) {
    RETURN_STRING_LITERAL(NOT_RETRANSMISSION);
    RETURN_STRING_LITERAL(HANDSHAKE_RETRANSMISSION);
    RETURN_STRING_LITERAL(LOSS_RETRANSMISSION);
    RETURN_STRING_LITERAL(ALL_UNACKED_RETRANSMISSION);
    RETURN_STRING_LITERAL(ALL_INITIAL_RETRANSMISSION);
    RETURN_STRING_LITERAL(RTO_RETRANSMISSION);
    RETURN_STRING_LITERAL(TLP_RETRANSMISSION);
    RETURN_STRING_LITERAL(PROBING_RETRANSMISSION);
  }
  return "INVALID_TRANSMISSION_TYPE";
}

string QuicUtils::PeerAddressChangeTypeToString(PeerAddressChangeType type) {
  switch (type) {
    RETURN_STRING_LITERAL(NO_CHANGE);
    RETURN_STRING_LITERAL(PORT_CHANGE);
    RETURN_STRING_LITERAL(IPV4_SUBNET_CHANGE);
    RETURN_STRING_LITERAL(IPV4_TO_IPV6_CHANGE);
    RETURN_STRING_LITERAL(IPV6_TO_IPV4_CHANGE);
    RETURN_STRING_LITERAL(IPV6_TO_IPV6_CHANGE);
    RETURN_STRING_LITERAL(IPV4_TO_IPV4_CHANGE);
  }
  return "INVALID_PEER_ADDRESS_CHANGE_TYPE";
}

// static
PeerAddressChangeType QuicUtils::DetermineAddressChangeType(
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
    // TODO(ckrasic) - this is unused without prefetch()
    // char* next_base = static_cast<char*>(iov.iov[iovnum + 1].iov_base);
    // char* next_base = static_cast<char*>(iov.iov[iovnum + 1].iov_base);
    // Prefetch 2 cachelines worth of data to get the prefetcher started; leave
    // it to the hardware prefetcher after that.
    // TODO(ckrasic) - investigate what to do about prefetch directives.
    // ::base::PrefetchT0(next_base);
    if (iov[iovnum + 1].iov_len >= 64) {
      // TODO(ckrasic) - investigate what to do about prefetch directives.
      // ::base::PrefetchT0(next_base + ABSL_CACHELINE_SIZE);
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

}  // namespace net
