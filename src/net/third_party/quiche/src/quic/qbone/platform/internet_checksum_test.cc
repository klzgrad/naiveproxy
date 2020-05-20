// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/platform/internet_checksum.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace {

// From the Numerical Example described in RFC 1071
// https://tools.ietf.org/html/rfc1071#section-3
TEST(InternetChecksumTest, MatchesRFC1071Example) {
  uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};

  InternetChecksum checksum;
  checksum.Update(data, 8);
  uint16_t result = checksum.Value();
  auto* result_bytes = reinterpret_cast<uint8_t*>(&result);
  ASSERT_EQ(0x22, result_bytes[0]);
  ASSERT_EQ(0x0d, result_bytes[1]);
}

// Same as above, except 7 bytes. Should behave as if there was an 8th byte
// that equals 0.
TEST(InternetChecksumTest, MatchesRFC1071ExampleWithOddByteCount) {
  uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6};

  InternetChecksum checksum;
  checksum.Update(data, 7);
  uint16_t result = checksum.Value();
  auto* result_bytes = reinterpret_cast<uint8_t*>(&result);
  ASSERT_EQ(0x23, result_bytes[0]);
  ASSERT_EQ(0x04, result_bytes[1]);
}

// From the example described at:
// http://www.cs.berkeley.edu/~kfall/EE122/lec06/tsld023.htm
TEST(InternetChecksumTest, MatchesBerkleyExample) {
  uint8_t data[] = {0xe3, 0x4f, 0x23, 0x96, 0x44, 0x27, 0x99, 0xf3};

  InternetChecksum checksum;
  checksum.Update(data, 8);
  uint16_t result = checksum.Value();
  auto* result_bytes = reinterpret_cast<uint8_t*>(&result);
  ASSERT_EQ(0x1a, result_bytes[0]);
  ASSERT_EQ(0xff, result_bytes[1]);
}

TEST(InternetChecksumTest, ChecksumRequiringMultipleCarriesInLittleEndian) {
  uint8_t data[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0x00};

  // Data will accumulate to 0x0002FFFF
  // Summing lower and upper halves gives 0x00010001
  // Second sum of lower and upper halves gives 0x0002
  // One's complement gives 0xfffd, or [0xfd, 0xff] in network byte order
  InternetChecksum checksum;
  checksum.Update(data, 8);
  uint16_t result = checksum.Value();
  auto* result_bytes = reinterpret_cast<uint8_t*>(&result);
  EXPECT_EQ(0xfd, result_bytes[0]);
  EXPECT_EQ(0xff, result_bytes[1]);
}

}  // namespace
}  // namespace quic
