// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_utils.h"

#include "net/third_party/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

class QuicUtilsTest : public QuicTest {};

TEST_F(QuicUtilsTest, DetermineAddressChangeType) {
  const QuicString kIPv4String1 = "1.2.3.4";
  const QuicString kIPv4String2 = "1.2.3.5";
  const QuicString kIPv4String3 = "1.1.3.5";
  const QuicString kIPv6String1 = "2001:700:300:1800::f";
  const QuicString kIPv6String2 = "2001:700:300:1800:1:1:1:f";
  QuicSocketAddress old_address;
  QuicSocketAddress new_address;
  QuicIpAddress address;

  EXPECT_EQ(NO_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));
  ASSERT_TRUE(address.FromString(kIPv4String1));
  old_address = QuicSocketAddress(address, 1234);
  EXPECT_EQ(NO_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));
  new_address = QuicSocketAddress(address, 1234);
  EXPECT_EQ(NO_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));

  new_address = QuicSocketAddress(address, 5678);
  EXPECT_EQ(PORT_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));
  ASSERT_TRUE(address.FromString(kIPv6String1));
  old_address = QuicSocketAddress(address, 1234);
  new_address = QuicSocketAddress(address, 5678);
  EXPECT_EQ(PORT_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));

  ASSERT_TRUE(address.FromString(kIPv4String1));
  old_address = QuicSocketAddress(address, 1234);
  ASSERT_TRUE(address.FromString(kIPv6String1));
  new_address = QuicSocketAddress(address, 1234);
  EXPECT_EQ(IPV4_TO_IPV6_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));

  old_address = QuicSocketAddress(address, 1234);
  ASSERT_TRUE(address.FromString(kIPv4String1));
  new_address = QuicSocketAddress(address, 1234);
  EXPECT_EQ(IPV6_TO_IPV4_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));

  ASSERT_TRUE(address.FromString(kIPv6String2));
  new_address = QuicSocketAddress(address, 1234);
  EXPECT_EQ(IPV6_TO_IPV6_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));

  ASSERT_TRUE(address.FromString(kIPv4String1));
  old_address = QuicSocketAddress(address, 1234);
  ASSERT_TRUE(address.FromString(kIPv4String2));
  new_address = QuicSocketAddress(address, 1234);
  EXPECT_EQ(IPV4_SUBNET_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));
  ASSERT_TRUE(address.FromString(kIPv4String3));
  new_address = QuicSocketAddress(address, 1234);
  EXPECT_EQ(IPV4_TO_IPV4_CHANGE,
            QuicUtils::DetermineAddressChangeType(old_address, new_address));
}

QuicUint128 IncrementalHashReference(const void* data, size_t len) {
  // The two constants are defined as part of the hash algorithm.
  // see http://www.isthe.com/chongo/tech/comp/fnv/
  // hash = 144066263297769815596495629667062367629
  QuicUint128 hash = MakeQuicUint128(UINT64_C(7809847782465536322),
                                     UINT64_C(7113472399480571277));
  // kPrime = 309485009821345068724781371
  const QuicUint128 kPrime = MakeQuicUint128(16777216, 315);
  const uint8_t* octets = reinterpret_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    hash = hash ^ MakeQuicUint128(0, octets[i]);
    hash = hash * kPrime;
  }
  return hash;
}

TEST_F(QuicUtilsTest, ReferenceTest) {
  std::vector<uint8_t> data(32);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = i % 255;
  }
  EXPECT_EQ(IncrementalHashReference(data.data(), data.size()),
            QuicUtils::FNV1a_128_Hash(QuicStringPiece(
                reinterpret_cast<const char*>(data.data()), data.size())));
}

TEST_F(QuicUtilsTest, IsUnackable) {
  for (size_t i = FIRST_PACKET_STATE; i <= LAST_PACKET_STATE; ++i) {
    if (i == NEVER_SENT || i == ACKED || i == UNACKABLE) {
      EXPECT_FALSE(QuicUtils::IsAckable(static_cast<SentPacketState>(i)));
    } else {
      EXPECT_TRUE(QuicUtils::IsAckable(static_cast<SentPacketState>(i)));
    }
  }
}

TEST_F(QuicUtilsTest, RetransmissionTypeToPacketState) {
  for (size_t i = FIRST_TRANSMISSION_TYPE; i <= LAST_TRANSMISSION_TYPE; ++i) {
    if (i == NOT_RETRANSMISSION) {
      continue;
    }
    SentPacketState state = QuicUtils::RetransmissionTypeToPacketState(
        static_cast<TransmissionType>(i));
    if (i == HANDSHAKE_RETRANSMISSION) {
      EXPECT_EQ(HANDSHAKE_RETRANSMITTED, state);
    } else if (i == LOSS_RETRANSMISSION) {
      EXPECT_EQ(LOST, state);
    } else if (i == ALL_UNACKED_RETRANSMISSION ||
               i == ALL_INITIAL_RETRANSMISSION) {
      EXPECT_EQ(UNACKABLE, state);
    } else if (i == TLP_RETRANSMISSION) {
      EXPECT_EQ(TLP_RETRANSMITTED, state);
    } else if (i == RTO_RETRANSMISSION) {
      EXPECT_EQ(RTO_RETRANSMITTED, state);
    } else if (i == PROBING_RETRANSMISSION) {
      EXPECT_EQ(PROBE_RETRANSMITTED, state);
    } else {
      DCHECK(false)
          << "No corresponding packet state according to transmission type: "
          << i;
    }
  }
}

TEST_F(QuicUtilsTest, IsIetfPacketHeader) {
  uint8_t first_byte = 0;
  EXPECT_TRUE(QuicUtils::IsIetfPacketHeader(first_byte));

  first_byte |= FLAGS_LONG_HEADER;
  EXPECT_TRUE(QuicUtils::IsIetfPacketHeader(first_byte));

  first_byte = 0;
  first_byte |= PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID;
  EXPECT_FALSE(QuicUtils::IsIetfPacketHeader(first_byte));
}

}  // namespace
}  // namespace test
}  // namespace quic
