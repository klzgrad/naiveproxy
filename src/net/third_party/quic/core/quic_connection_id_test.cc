// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/quic_connection_id.h"

#include <cstdint>
#include <cstring>

#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/test_tools/quic_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace quic {

namespace {

class QuicConnectionIdTest : public QuicTest {};

TEST_F(QuicConnectionIdTest, Empty) {
  QuicConnectionId connection_id_empty = EmptyQuicConnectionId();
  EXPECT_TRUE(connection_id_empty.IsEmpty());
}

TEST_F(QuicConnectionIdTest, DefaultIsEmpty) {
  QuicConnectionId connection_id_empty = QuicConnectionId();
  EXPECT_TRUE(connection_id_empty.IsEmpty());
}

TEST_F(QuicConnectionIdTest, NotEmpty) {
  QuicConnectionId connection_id = test::TestConnectionId(1);
  EXPECT_FALSE(connection_id.IsEmpty());
}

TEST_F(QuicConnectionIdTest, ZeroIsNotEmpty) {
  QuicConnectionId connection_id = test::TestConnectionId(0);
  if (!GetQuicRestartFlag(quic_connection_ids_network_byte_order)) {
    // Zero is empty when connection IDs are represented in host byte order.
    return;
  }
  EXPECT_FALSE(connection_id.IsEmpty());
}

TEST_F(QuicConnectionIdTest, Data) {
  if (!GetQuicRestartFlag(quic_connection_ids_network_byte_order)) {
    // These methods are not allowed when the flag is off.
    return;
  }
  char connection_id_data[kQuicDefaultConnectionIdLength];
  memset(connection_id_data, 0x42, sizeof(connection_id_data));
  QuicConnectionId connection_id1 =
      QuicConnectionId(connection_id_data, sizeof(connection_id_data));
  QuicConnectionId connection_id2 =
      QuicConnectionId(connection_id_data, sizeof(connection_id_data));
  EXPECT_EQ(connection_id1, connection_id2);
  EXPECT_EQ(connection_id1.length(), kQuicDefaultConnectionIdLength);
  EXPECT_EQ(connection_id1.data(), connection_id1.mutable_data());
  EXPECT_EQ(0, memcmp(connection_id1.data(), connection_id2.data(),
                      sizeof(connection_id_data)));
  EXPECT_EQ(0, memcmp(connection_id1.data(), connection_id_data,
                      sizeof(connection_id_data)));
  connection_id2.mutable_data()[0] = 0x33;
  EXPECT_NE(connection_id1, connection_id2);
  static const uint8_t kNewLength = 4;
  connection_id2.set_length(kNewLength);
  EXPECT_EQ(kNewLength, connection_id2.length());
}

TEST_F(QuicConnectionIdTest, DoubleConvert) {
  QuicConnectionId connection_id64_1 = test::TestConnectionId(1);
  QuicConnectionId connection_id64_2 = test::TestConnectionId(42);
  QuicConnectionId connection_id64_3 =
      test::TestConnectionId(UINT64_C(0xfedcba9876543210));
  EXPECT_EQ(connection_id64_1,
            test::TestConnectionId(
                test::TestConnectionIdToUInt64(connection_id64_1)));
  EXPECT_EQ(connection_id64_2,
            test::TestConnectionId(
                test::TestConnectionIdToUInt64(connection_id64_2)));
  EXPECT_EQ(connection_id64_3,
            test::TestConnectionId(
                test::TestConnectionIdToUInt64(connection_id64_3)));
  EXPECT_NE(connection_id64_1, connection_id64_2);
  EXPECT_NE(connection_id64_1, connection_id64_3);
  EXPECT_NE(connection_id64_2, connection_id64_3);
}

TEST_F(QuicConnectionIdTest, Hash) {
  QuicConnectionId connection_id64_1 = test::TestConnectionId(1);
  QuicConnectionId connection_id64_1b = test::TestConnectionId(1);
  QuicConnectionId connection_id64_2 = test::TestConnectionId(42);
  QuicConnectionId connection_id64_3 =
      test::TestConnectionId(UINT64_C(0xfedcba9876543210));
  EXPECT_EQ(connection_id64_1.Hash(), connection_id64_1b.Hash());
  EXPECT_NE(connection_id64_1.Hash(), connection_id64_2.Hash());
  EXPECT_NE(connection_id64_1.Hash(), connection_id64_3.Hash());
  EXPECT_NE(connection_id64_2.Hash(), connection_id64_3.Hash());
}

}  // namespace

}  // namespace quic
