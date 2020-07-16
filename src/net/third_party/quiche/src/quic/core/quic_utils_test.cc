// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_utils.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
namespace {

class QuicUtilsTest : public QuicTest {};

TEST_F(QuicUtilsTest, DetermineAddressChangeType) {
  const std::string kIPv4String1 = "1.2.3.4";
  const std::string kIPv4String2 = "1.2.3.5";
  const std::string kIPv4String3 = "1.1.3.5";
  const std::string kIPv6String1 = "2001:700:300:1800::f";
  const std::string kIPv6String2 = "2001:700:300:1800:1:1:1:f";
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
            QuicUtils::FNV1a_128_Hash(quiche::QuicheStringPiece(
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
    } else if (i == PTO_RETRANSMISSION) {
      EXPECT_EQ(PTO_RETRANSMITTED, state);
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
  // IETF QUIC short header
  uint8_t first_byte = 0;
  EXPECT_TRUE(QuicUtils::IsIetfPacketHeader(first_byte));
  EXPECT_TRUE(QuicUtils::IsIetfPacketShortHeader(first_byte));

  // IETF QUIC long header
  first_byte |= (FLAGS_LONG_HEADER | FLAGS_DEMULTIPLEXING_BIT);
  EXPECT_TRUE(QuicUtils::IsIetfPacketHeader(first_byte));
  EXPECT_FALSE(QuicUtils::IsIetfPacketShortHeader(first_byte));

  // IETF QUIC long header, version negotiation.
  first_byte = 0;
  first_byte |= FLAGS_LONG_HEADER;
  EXPECT_TRUE(QuicUtils::IsIetfPacketHeader(first_byte));
  EXPECT_FALSE(QuicUtils::IsIetfPacketShortHeader(first_byte));

  // GQUIC
  first_byte = 0;
  first_byte |= PACKET_PUBLIC_FLAGS_8BYTE_CONNECTION_ID;
  EXPECT_FALSE(QuicUtils::IsIetfPacketHeader(first_byte));
  EXPECT_FALSE(QuicUtils::IsIetfPacketShortHeader(first_byte));
}

TEST_F(QuicUtilsTest, ReplacementConnectionIdIsDeterministic) {
  // Verify that two equal connection IDs get the same replacement.
  QuicConnectionId connection_id64a = TestConnectionId(33);
  QuicConnectionId connection_id64b = TestConnectionId(33);
  EXPECT_EQ(connection_id64a, connection_id64b);
  EXPECT_EQ(QuicUtils::CreateReplacementConnectionId(connection_id64a),
            QuicUtils::CreateReplacementConnectionId(connection_id64b));
  QuicConnectionId connection_id72a = TestConnectionIdNineBytesLong(42);
  QuicConnectionId connection_id72b = TestConnectionIdNineBytesLong(42);
  EXPECT_EQ(connection_id72a, connection_id72b);
  EXPECT_EQ(QuicUtils::CreateReplacementConnectionId(connection_id72a),
            QuicUtils::CreateReplacementConnectionId(connection_id72b));
}

TEST_F(QuicUtilsTest, ReplacementConnectionIdLengthIsCorrect) {
  // Verify that all lengths get replaced by kQuicDefaultConnectionIdLength.
  const char connection_id_bytes[255] = {};
  for (uint8_t i = 0; i < sizeof(connection_id_bytes) - 1; ++i) {
    QuicConnectionId connection_id(connection_id_bytes, i);
    QuicConnectionId replacement_connection_id =
        QuicUtils::CreateReplacementConnectionId(connection_id);
    EXPECT_EQ(kQuicDefaultConnectionIdLength,
              replacement_connection_id.length());
  }
}

TEST_F(QuicUtilsTest, ReplacementConnectionIdHasEntropy) {
  // Make sure all these test connection IDs have different replacements.
  for (uint64_t i = 0; i < 256; ++i) {
    QuicConnectionId connection_id_i = TestConnectionId(i);
    EXPECT_NE(connection_id_i,
              QuicUtils::CreateReplacementConnectionId(connection_id_i));
    for (uint64_t j = i + 1; j <= 256; ++j) {
      QuicConnectionId connection_id_j = TestConnectionId(j);
      EXPECT_NE(connection_id_i, connection_id_j);
      EXPECT_NE(QuicUtils::CreateReplacementConnectionId(connection_id_i),
                QuicUtils::CreateReplacementConnectionId(connection_id_j));
    }
  }
}

TEST_F(QuicUtilsTest, RandomConnectionId) {
  MockRandom random(33);
  QuicConnectionId connection_id = QuicUtils::CreateRandomConnectionId(&random);
  EXPECT_EQ(connection_id.length(), sizeof(uint64_t));
  char connection_id_bytes[sizeof(uint64_t)];
  random.RandBytes(connection_id_bytes, QUICHE_ARRAYSIZE(connection_id_bytes));
  EXPECT_EQ(connection_id,
            QuicConnectionId(static_cast<char*>(connection_id_bytes),
                             QUICHE_ARRAYSIZE(connection_id_bytes)));
  EXPECT_NE(connection_id, EmptyQuicConnectionId());
  EXPECT_NE(connection_id, TestConnectionId());
  EXPECT_NE(connection_id, TestConnectionId(1));
  EXPECT_NE(connection_id, TestConnectionIdNineBytesLong(1));
  EXPECT_EQ(QuicUtils::CreateRandomConnectionId().length(),
            kQuicDefaultConnectionIdLength);
}

TEST_F(QuicUtilsTest, RandomConnectionIdVariableLength) {
  MockRandom random(1337);
  const uint8_t connection_id_length = 9;
  QuicConnectionId connection_id =
      QuicUtils::CreateRandomConnectionId(connection_id_length, &random);
  EXPECT_EQ(connection_id.length(), connection_id_length);
  char connection_id_bytes[connection_id_length];
  random.RandBytes(connection_id_bytes, QUICHE_ARRAYSIZE(connection_id_bytes));
  EXPECT_EQ(connection_id,
            QuicConnectionId(static_cast<char*>(connection_id_bytes),
                             QUICHE_ARRAYSIZE(connection_id_bytes)));
  EXPECT_NE(connection_id, EmptyQuicConnectionId());
  EXPECT_NE(connection_id, TestConnectionId());
  EXPECT_NE(connection_id, TestConnectionId(1));
  EXPECT_NE(connection_id, TestConnectionIdNineBytesLong(1));
  EXPECT_EQ(QuicUtils::CreateRandomConnectionId(connection_id_length).length(),
            connection_id_length);
}

TEST_F(QuicUtilsTest, VariableLengthConnectionId) {
  EXPECT_FALSE(VersionAllowsVariableLengthConnectionIds(QUIC_VERSION_43));
  EXPECT_TRUE(QuicUtils::IsConnectionIdValidForVersion(
      QuicUtils::CreateZeroConnectionId(QUIC_VERSION_43), QUIC_VERSION_43));
  EXPECT_TRUE(QuicUtils::IsConnectionIdValidForVersion(
      QuicUtils::CreateZeroConnectionId(QUIC_VERSION_50), QUIC_VERSION_50));
  EXPECT_NE(QuicUtils::CreateZeroConnectionId(QUIC_VERSION_43),
            EmptyQuicConnectionId());
  EXPECT_EQ(QuicUtils::CreateZeroConnectionId(QUIC_VERSION_50),
            EmptyQuicConnectionId());
  EXPECT_FALSE(QuicUtils::IsConnectionIdValidForVersion(EmptyQuicConnectionId(),
                                                        QUIC_VERSION_43));
}

TEST_F(QuicUtilsTest, StatelessResetToken) {
  QuicConnectionId connection_id1a = test::TestConnectionId(1);
  QuicConnectionId connection_id1b = test::TestConnectionId(1);
  QuicConnectionId connection_id2 = test::TestConnectionId(2);
  QuicUint128 token1a = QuicUtils::GenerateStatelessResetToken(connection_id1a);
  QuicUint128 token1b = QuicUtils::GenerateStatelessResetToken(connection_id1b);
  QuicUint128 token2 = QuicUtils::GenerateStatelessResetToken(connection_id2);
  EXPECT_EQ(token1a, token1b);
  EXPECT_NE(token1a, token2);
}

enum class TestEnumClassBit : uint8_t {
  BIT_ZERO = 0,
  BIT_ONE,
  BIT_TWO,
};

enum TestEnumBit {
  TEST_BIT_0 = 0,
  TEST_BIT_1,
  TEST_BIT_2,
};

TEST(QuicBitMaskTest, EnumClass) {
  BitMask64 mask(TestEnumClassBit::BIT_ZERO, TestEnumClassBit::BIT_TWO);
  EXPECT_TRUE(mask.IsSet(TestEnumClassBit::BIT_ZERO));
  EXPECT_FALSE(mask.IsSet(TestEnumClassBit::BIT_ONE));
  EXPECT_TRUE(mask.IsSet(TestEnumClassBit::BIT_TWO));

  mask.ClearAll();
  EXPECT_FALSE(mask.IsSet(TestEnumClassBit::BIT_ZERO));
  EXPECT_FALSE(mask.IsSet(TestEnumClassBit::BIT_ONE));
  EXPECT_FALSE(mask.IsSet(TestEnumClassBit::BIT_TWO));
}

TEST(QuicBitMaskTest, Enum) {
  BitMask64 mask(TEST_BIT_1, TEST_BIT_2);
  EXPECT_FALSE(mask.IsSet(TEST_BIT_0));
  EXPECT_TRUE(mask.IsSet(TEST_BIT_1));
  EXPECT_TRUE(mask.IsSet(TEST_BIT_2));

  mask.ClearAll();
  EXPECT_FALSE(mask.IsSet(TEST_BIT_0));
  EXPECT_FALSE(mask.IsSet(TEST_BIT_1));
  EXPECT_FALSE(mask.IsSet(TEST_BIT_2));
}

TEST(QuicBitMaskTest, Integer) {
  BitMask64 mask(1, 3);
  mask.Set(3);
  mask.Set(5, 7, 9);
  EXPECT_FALSE(mask.IsSet(0));
  EXPECT_TRUE(mask.IsSet(1));
  EXPECT_FALSE(mask.IsSet(2));
  EXPECT_TRUE(mask.IsSet(3));
  EXPECT_FALSE(mask.IsSet(4));
  EXPECT_TRUE(mask.IsSet(5));
  EXPECT_FALSE(mask.IsSet(6));
  EXPECT_TRUE(mask.IsSet(7));
  EXPECT_FALSE(mask.IsSet(8));
  EXPECT_TRUE(mask.IsSet(9));
}

TEST(QuicBitMaskTest, NumBits) {
  EXPECT_EQ(64u, BitMask64::NumBits());
  EXPECT_EQ(32u, BitMask<uint32_t>::NumBits());
}

TEST(QuicBitMaskTest, Constructor) {
  BitMask64 empty_mask;
  for (size_t bit = 0; bit < empty_mask.NumBits(); ++bit) {
    EXPECT_FALSE(empty_mask.IsSet(bit));
  }

  BitMask64 mask(1, 3);
  BitMask64 mask2 = mask;
  BitMask64 mask3(mask2);

  for (size_t bit = 0; bit < mask.NumBits(); ++bit) {
    EXPECT_EQ(mask.IsSet(bit), mask2.IsSet(bit));
    EXPECT_EQ(mask.IsSet(bit), mask3.IsSet(bit));
  }

  EXPECT_TRUE(std::is_trivially_copyable<BitMask64>::value);
}

}  // namespace
}  // namespace test
}  // namespace quic
