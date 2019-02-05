// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/api/quic_endian.h"

#include "net/third_party/quic/platform/api/quic_test.h"

namespace quic {
namespace test {
namespace {

const uint16_t k16BitTestData = 0xaabb;
const uint16_t k16BitSwappedTestData = 0xbbaa;
const uint32_t k32BitTestData = 0xaabbccdd;
const uint32_t k32BitSwappedTestData = 0xddccbbaa;
const uint64_t k64BitTestData = 0xaabbccdd44332211;
const uint64_t k64BitSwappedTestData = 0x11223344ddccbbaa;

class QuicEndianTest : public QuicTest {};

TEST_F(QuicEndianTest, HostToNet) {
  if (QuicEndian::HostIsLittleEndian()) {
    EXPECT_EQ(k16BitSwappedTestData, QuicEndian::HostToNet16(k16BitTestData));
    EXPECT_EQ(k32BitSwappedTestData, QuicEndian::HostToNet32(k32BitTestData));
    EXPECT_EQ(k64BitSwappedTestData, QuicEndian::HostToNet64(k64BitTestData));
  } else {
    EXPECT_EQ(k16BitTestData, QuicEndian::HostToNet16(k16BitTestData));
    EXPECT_EQ(k32BitTestData, QuicEndian::HostToNet32(k32BitTestData));
    EXPECT_EQ(k64BitTestData, QuicEndian::HostToNet64(k64BitTestData));
  }
}

TEST_F(QuicEndianTest, NetToHost) {
  if (QuicEndian::HostIsLittleEndian()) {
    EXPECT_EQ(k16BitTestData, QuicEndian::NetToHost16(k16BitSwappedTestData));
    EXPECT_EQ(k32BitTestData, QuicEndian::NetToHost32(k32BitSwappedTestData));
    EXPECT_EQ(k64BitTestData, QuicEndian::NetToHost64(k64BitSwappedTestData));
  } else {
    EXPECT_EQ(k16BitSwappedTestData,
              QuicEndian::NetToHost16(k16BitSwappedTestData));
    EXPECT_EQ(k32BitSwappedTestData,
              QuicEndian::NetToHost32(k32BitSwappedTestData));
    EXPECT_EQ(k64BitSwappedTestData,
              QuicEndian::NetToHost64(k64BitSwappedTestData));
  }
}

}  // namespace
}  // namespace test
}  // namespace quic
