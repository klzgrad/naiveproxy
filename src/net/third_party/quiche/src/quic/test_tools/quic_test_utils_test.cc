// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class QuicTestUtilsTest : public QuicTest {};

TEST_F(QuicTestUtilsTest, ConnectionId) {
  EXPECT_NE(EmptyQuicConnectionId(), TestConnectionId());
  EXPECT_NE(EmptyQuicConnectionId(), TestConnectionId(1));
  EXPECT_EQ(TestConnectionId(), TestConnectionId());
  EXPECT_EQ(TestConnectionId(33), TestConnectionId(33));
  EXPECT_NE(TestConnectionId(0xdead), TestConnectionId(0xbeef));
  EXPECT_EQ(0x1337u, TestConnectionIdToUInt64(TestConnectionId(0x1337)));
  EXPECT_NE(0xdeadu, TestConnectionIdToUInt64(TestConnectionId(0xbeef)));
}

TEST_F(QuicTestUtilsTest, BasicApproxEq) {
  EXPECT_APPROX_EQ(10, 10, 1e-6f);
  EXPECT_APPROX_EQ(1000, 1001, 0.01f);
  EXPECT_NONFATAL_FAILURE(EXPECT_APPROX_EQ(1000, 1100, 0.01f), "");

  EXPECT_APPROX_EQ(64, 31, 0.55f);
  EXPECT_NONFATAL_FAILURE(EXPECT_APPROX_EQ(31, 64, 0.55f), "");
}

TEST_F(QuicTestUtilsTest, QuicTimeDelta) {
  EXPECT_APPROX_EQ(QuicTime::Delta::FromMicroseconds(1000),
                   QuicTime::Delta::FromMicroseconds(1003), 0.01f);
  EXPECT_NONFATAL_FAILURE(
      EXPECT_APPROX_EQ(QuicTime::Delta::FromMicroseconds(1000),
                       QuicTime::Delta::FromMicroseconds(1200), 0.01f),
      "");
}

TEST_F(QuicTestUtilsTest, QuicBandwidth) {
  EXPECT_APPROX_EQ(QuicBandwidth::FromBytesPerSecond(1000),
                   QuicBandwidth::FromBitsPerSecond(8005), 0.01f);
  EXPECT_NONFATAL_FAILURE(
      EXPECT_APPROX_EQ(QuicBandwidth::FromBytesPerSecond(1000),
                       QuicBandwidth::FromBitsPerSecond(9005), 0.01f),
      "");
}

// Ensure that SimpleRandom does not change its output for a fixed seed.
TEST_F(QuicTestUtilsTest, SimpleRandomStability) {
  SimpleRandom rng;
  rng.set_seed(UINT64_C(0x1234567800010001));
  EXPECT_EQ(UINT64_C(12589383305231984671), rng.RandUint64());
  EXPECT_EQ(UINT64_C(17775425089941798664), rng.RandUint64());
}

// Ensure that the output of SimpleRandom does not depend on the size of the
// read calls.
TEST_F(QuicTestUtilsTest, SimpleRandomChunks) {
  SimpleRandom rng;
  std::string reference(16 * 1024, '\0');
  rng.RandBytes(&reference[0], reference.size());

  for (size_t chunk_size : {3, 4, 7, 4096}) {
    rng.set_seed(0);
    size_t chunks = reference.size() / chunk_size;
    std::string buffer(chunks * chunk_size, '\0');
    for (size_t i = 0; i < chunks; i++) {
      rng.RandBytes(&buffer[i * chunk_size], chunk_size);
    }
    EXPECT_EQ(reference.substr(0, buffer.size()), buffer)
        << "Failed for chunk_size = " << chunk_size;
  }
}

}  // namespace test
}  // namespace quic
