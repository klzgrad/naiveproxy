// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

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
  EXPECT_FALSE(connection_id.IsEmpty());
}

TEST_F(QuicConnectionIdTest, Data) {
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

  // Verify that any two all-zero connection IDs of different lengths never
  // have the same hash.
  const char connection_id_bytes[255] = {};
  for (uint8_t i = 0; i < sizeof(connection_id_bytes) - 1; ++i) {
    QuicConnectionId connection_id_i(connection_id_bytes, i);
    for (uint8_t j = i + 1; j < sizeof(connection_id_bytes); ++j) {
      QuicConnectionId connection_id_j(connection_id_bytes, j);
      EXPECT_NE(connection_id_i.Hash(), connection_id_j.Hash());
    }
  }
}

TEST_F(QuicConnectionIdTest, AssignAndCopy) {
  QuicConnectionId connection_id = test::TestConnectionId(1);
  QuicConnectionId connection_id2 = test::TestConnectionId(2);
  connection_id = connection_id2;
  EXPECT_EQ(connection_id, test::TestConnectionId(2));
  EXPECT_NE(connection_id, test::TestConnectionId(1));
  connection_id = QuicConnectionId(test::TestConnectionId(1));
  EXPECT_EQ(connection_id, test::TestConnectionId(1));
  EXPECT_NE(connection_id, test::TestConnectionId(2));
}

TEST_F(QuicConnectionIdTest, ChangeLength) {
  QuicConnectionId connection_id64_1 = test::TestConnectionId(1);
  QuicConnectionId connection_id64_2 = test::TestConnectionId(2);
  QuicConnectionId connection_id136_2 = test::TestConnectionId(2);
  connection_id136_2.set_length(17);
  memset(connection_id136_2.mutable_data() + 8, 0, 9);
  char connection_id136_2_bytes[17] = {0, 0, 0, 0, 0, 0, 0, 2, 0,
                                       0, 0, 0, 0, 0, 0, 0, 0};
  QuicConnectionId connection_id136_2b(connection_id136_2_bytes,
                                       sizeof(connection_id136_2_bytes));
  EXPECT_EQ(connection_id136_2, connection_id136_2b);
  QuicConnectionId connection_id = connection_id64_1;
  connection_id.set_length(17);
  EXPECT_NE(connection_id64_1, connection_id);
  // Check resizing big to small.
  connection_id.set_length(8);
  EXPECT_EQ(connection_id64_1, connection_id);
  // Check resizing small to big.
  connection_id.set_length(17);
  memset(connection_id.mutable_data(), 0, connection_id.length());
  memcpy(connection_id.mutable_data(), connection_id64_2.data(),
         connection_id64_2.length());
  EXPECT_EQ(connection_id136_2, connection_id);
  EXPECT_EQ(connection_id136_2b, connection_id);
  QuicConnectionId connection_id120(connection_id136_2_bytes, 15);
  connection_id.set_length(15);
  EXPECT_EQ(connection_id120, connection_id);
  // Check resizing big to big.
  QuicConnectionId connection_id2 = connection_id120;
  connection_id2.set_length(17);
  connection_id2.mutable_data()[15] = 0;
  connection_id2.mutable_data()[16] = 0;
  EXPECT_EQ(connection_id136_2, connection_id2);
  EXPECT_EQ(connection_id136_2b, connection_id2);
}

}  // namespace

}  // namespace quic
