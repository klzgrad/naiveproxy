// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/common/platform/api/quiche_endian.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_test.h"

namespace quiche {
namespace test {
namespace {

const uint16_t k16BitTestData = 0xaabb;
const uint16_t k16BitSwappedTestData = 0xbbaa;
const uint32_t k32BitTestData = 0xaabbccdd;
const uint32_t k32BitSwappedTestData = 0xddccbbaa;
const uint64_t k64BitTestData = 0xaabbccdd44332211;
const uint64_t k64BitSwappedTestData = 0x11223344ddccbbaa;

class QuicheEndianTest : public QuicheTest {};

TEST_F(QuicheEndianTest, HostToNet) {
  if (quiche::QuicheEndian::HostIsLittleEndian()) {
    EXPECT_EQ(k16BitSwappedTestData,
              quiche::QuicheEndian::HostToNet16(k16BitTestData));
    EXPECT_EQ(k32BitSwappedTestData,
              quiche::QuicheEndian::HostToNet32(k32BitTestData));
    EXPECT_EQ(k64BitSwappedTestData,
              quiche::QuicheEndian::HostToNet64(k64BitTestData));
  } else {
    EXPECT_EQ(k16BitTestData,
              quiche::QuicheEndian::HostToNet16(k16BitTestData));
    EXPECT_EQ(k32BitTestData,
              quiche::QuicheEndian::HostToNet32(k32BitTestData));
    EXPECT_EQ(k64BitTestData,
              quiche::QuicheEndian::HostToNet64(k64BitTestData));
  }
}

TEST_F(QuicheEndianTest, NetToHost) {
  if (quiche::QuicheEndian::HostIsLittleEndian()) {
    EXPECT_EQ(k16BitTestData,
              quiche::QuicheEndian::NetToHost16(k16BitSwappedTestData));
    EXPECT_EQ(k32BitTestData,
              quiche::QuicheEndian::NetToHost32(k32BitSwappedTestData));
    EXPECT_EQ(k64BitTestData,
              quiche::QuicheEndian::NetToHost64(k64BitSwappedTestData));
  } else {
    EXPECT_EQ(k16BitSwappedTestData,
              quiche::QuicheEndian::NetToHost16(k16BitSwappedTestData));
    EXPECT_EQ(k32BitSwappedTestData,
              quiche::QuicheEndian::NetToHost32(k32BitSwappedTestData));
    EXPECT_EQ(k64BitSwappedTestData,
              quiche::QuicheEndian::NetToHost64(k64BitSwappedTestData));
  }
}

}  // namespace
}  // namespace test
}  // namespace quiche
