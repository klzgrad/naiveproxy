// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {
namespace test {

class NullDecrypterTest : public QuicTestWithParam<bool> {};

TEST_F(NullDecrypterTest, DecryptClient) {
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
  const char* data = reinterpret_cast<const char*>(expected);
  size_t len = QUICHE_ARRAYSIZE(expected);
  NullDecrypter decrypter(Perspective::IS_SERVER);
  char buffer[256];
  size_t length = 0;
  ASSERT_TRUE(decrypter.DecryptPacket(0, "hello world!",
                                      quiche::QuicheStringPiece(data, len),
                                      buffer, &length, 256));
  EXPECT_LT(0u, length);
  EXPECT_EQ("goodbye!", quiche::QuicheStringPiece(buffer, length));
}

TEST_F(NullDecrypterTest, DecryptServer) {
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
  const char* data = reinterpret_cast<const char*>(expected);
  size_t len = QUICHE_ARRAYSIZE(expected);
  NullDecrypter decrypter(Perspective::IS_CLIENT);
  char buffer[256];
  size_t length = 0;
  ASSERT_TRUE(decrypter.DecryptPacket(0, "hello world!",
                                      quiche::QuicheStringPiece(data, len),
                                      buffer, &length, 256));
  EXPECT_LT(0u, length);
  EXPECT_EQ("goodbye!", quiche::QuicheStringPiece(buffer, length));
}

TEST_F(NullDecrypterTest, BadHash) {
  unsigned char expected[] = {
      // fnv hash
      0x46,
      0x11,
      0xea,
      0x5f,
      0xcf,
      0x1d,
      0x66,
      0x5b,
      0xba,
      0xf0,
      0xbc,
      0xfd,
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
  const char* data = reinterpret_cast<const char*>(expected);
  size_t len = QUICHE_ARRAYSIZE(expected);
  NullDecrypter decrypter(Perspective::IS_CLIENT);
  char buffer[256];
  size_t length = 0;
  ASSERT_FALSE(decrypter.DecryptPacket(0, "hello world!",
                                       quiche::QuicheStringPiece(data, len),
                                       buffer, &length, 256));
}

TEST_F(NullDecrypterTest, ShortInput) {
  unsigned char expected[] = {
      // fnv hash (truncated)
      0x46, 0x11, 0xea, 0x5f, 0xcf, 0x1d, 0x66, 0x5b, 0xba, 0xf0, 0xbc,
  };
  const char* data = reinterpret_cast<const char*>(expected);
  size_t len = QUICHE_ARRAYSIZE(expected);
  NullDecrypter decrypter(Perspective::IS_CLIENT);
  char buffer[256];
  size_t length = 0;
  ASSERT_FALSE(decrypter.DecryptPacket(0, "hello world!",
                                       quiche::QuicheStringPiece(data, len),
                                       buffer, &length, 256));
}

}  // namespace test
}  // namespace quic
