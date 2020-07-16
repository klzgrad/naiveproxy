// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/test_tools/quiche_test_utils.h"

namespace quic {
namespace test {

class NullEncrypterTest : public QuicTestWithParam<bool> {};

TEST_F(NullEncrypterTest, EncryptClient) {
  unsigned char expected[] = {
      // fnv hash
      0x97,
      0xdc,
      0x27,
      0x2f,
      0x18,
      0xa8,
      0x56,
      0x73,
      0xdf,
      0x8d,
      0x1d,
      0xd0,
      // payload
      'g',
      'o',
      'o',
      'd',
      'b',
      'y',
      'e',
      '!',
  };
  char encrypted[256];
  size_t encrypted_len = 0;
  NullEncrypter encrypter(Perspective::IS_CLIENT);
  ASSERT_TRUE(encrypter.EncryptPacket(0, "hello world!", "goodbye!", encrypted,
                                      &encrypted_len, 256));
  quiche::test::CompareCharArraysWithHexError(
      "encrypted data", encrypted, encrypted_len,
      reinterpret_cast<const char*>(expected), QUICHE_ARRAYSIZE(expected));
}

TEST_F(NullEncrypterTest, EncryptServer) {
  unsigned char expected[] = {
      // fnv hash
      0x63,
      0x5e,
      0x08,
      0x03,
      0x32,
      0x80,
      0x8f,
      0x73,
      0xdf,
      0x8d,
      0x1d,
      0x1a,
      // payload
      'g',
      'o',
      'o',
      'd',
      'b',
      'y',
      'e',
      '!',
  };
  char encrypted[256];
  size_t encrypted_len = 0;
  NullEncrypter encrypter(Perspective::IS_SERVER);
  ASSERT_TRUE(encrypter.EncryptPacket(0, "hello world!", "goodbye!", encrypted,
                                      &encrypted_len, 256));
  quiche::test::CompareCharArraysWithHexError(
      "encrypted data", encrypted, encrypted_len,
      reinterpret_cast<const char*>(expected), QUICHE_ARRAYSIZE(expected));
}

TEST_F(NullEncrypterTest, GetMaxPlaintextSize) {
  NullEncrypter encrypter(Perspective::IS_CLIENT);
  EXPECT_EQ(1000u, encrypter.GetMaxPlaintextSize(1012));
  EXPECT_EQ(100u, encrypter.GetMaxPlaintextSize(112));
  EXPECT_EQ(10u, encrypter.GetMaxPlaintextSize(22));
  EXPECT_EQ(0u, encrypter.GetMaxPlaintextSize(11));
}

TEST_F(NullEncrypterTest, GetCiphertextSize) {
  NullEncrypter encrypter(Perspective::IS_CLIENT);
  EXPECT_EQ(1012u, encrypter.GetCiphertextSize(1000));
  EXPECT_EQ(112u, encrypter.GetCiphertextSize(100));
  EXPECT_EQ(22u, encrypter.GetCiphertextSize(10));
}

}  // namespace test
}  // namespace quic
