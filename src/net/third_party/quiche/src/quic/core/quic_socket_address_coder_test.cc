// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_socket_address_coder.h"

#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"

namespace quic {
namespace test {

class QuicSocketAddressCoderTest : public QuicTest {};

TEST_F(QuicSocketAddressCoderTest, EncodeIPv4) {
  QuicIpAddress ip;
  ip.FromString("4.31.198.44");
  QuicSocketAddressCoder coder(QuicSocketAddress(ip, 0x1234));
  std::string serialized = coder.Encode();
  std::string expected("\x02\x00\x04\x1f\xc6\x2c\x34\x12", 8);
  EXPECT_EQ(expected, serialized);
}

TEST_F(QuicSocketAddressCoderTest, EncodeIPv6) {
  QuicIpAddress ip;
  ip.FromString("2001:700:300:1800::f");
  QuicSocketAddressCoder coder(QuicSocketAddress(ip, 0x5678));
  std::string serialized = coder.Encode();
  std::string expected(
      "\x0a\x00"
      "\x20\x01\x07\x00\x03\x00\x18\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x0f"
      "\x78\x56",
      20);
  EXPECT_EQ(expected, serialized);
}

TEST_F(QuicSocketAddressCoderTest, DecodeIPv4) {
  std::string serialized("\x02\x00\x04\x1f\xc6\x2c\x34\x12", 8);
  QuicSocketAddressCoder coder;
  ASSERT_TRUE(coder.Decode(serialized.data(), serialized.length()));
  EXPECT_EQ(IpAddressFamily::IP_V4, coder.ip().address_family());
  std::string expected_addr("\x04\x1f\xc6\x2c");
  EXPECT_EQ(expected_addr, coder.ip().ToPackedString());
  EXPECT_EQ(0x1234, coder.port());
}

TEST_F(QuicSocketAddressCoderTest, DecodeIPv6) {
  std::string serialized(
      "\x0a\x00"
      "\x20\x01\x07\x00\x03\x00\x18\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x0f"
      "\x78\x56",
      20);
  QuicSocketAddressCoder coder;
  ASSERT_TRUE(coder.Decode(serialized.data(), serialized.length()));
  EXPECT_EQ(IpAddressFamily::IP_V6, coder.ip().address_family());
  std::string expected_addr(
      "\x20\x01\x07\x00\x03\x00\x18\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x0f",
      16);
  EXPECT_EQ(expected_addr, coder.ip().ToPackedString());
  EXPECT_EQ(0x5678, coder.port());
}

TEST_F(QuicSocketAddressCoderTest, DecodeBad) {
  std::string serialized(
      "\x0a\x00"
      "\x20\x01\x07\x00\x03\x00\x18\x00"
      "\x00\x00\x00\x00\x00\x00\x00\x0f"
      "\x78\x56",
      20);
  QuicSocketAddressCoder coder;
  EXPECT_TRUE(coder.Decode(serialized.data(), serialized.length()));
  // Append junk.
  serialized.push_back('\0');
  EXPECT_FALSE(coder.Decode(serialized.data(), serialized.length()));
  // Undo.
  serialized.resize(20);
  EXPECT_TRUE(coder.Decode(serialized.data(), serialized.length()));

  // Set an unknown address family.
  serialized[0] = '\x03';
  EXPECT_FALSE(coder.Decode(serialized.data(), serialized.length()));
  // Undo.
  serialized[0] = '\x0a';
  EXPECT_TRUE(coder.Decode(serialized.data(), serialized.length()));

  // Truncate.
  size_t len = serialized.length();
  for (size_t i = 0; i < len; i++) {
    ASSERT_FALSE(serialized.empty());
    serialized.erase(serialized.length() - 1);
    EXPECT_FALSE(coder.Decode(serialized.data(), serialized.length()));
  }
  EXPECT_TRUE(serialized.empty());
}

TEST_F(QuicSocketAddressCoderTest, EncodeAndDecode) {
  struct {
    const char* ip_literal;
    uint16_t port;
  } test_case[] = {
      {"93.184.216.119", 0x1234},
      {"199.204.44.194", 80},
      {"149.20.4.69", 443},
      {"127.0.0.1", 8080},
      {"2001:700:300:1800::", 0x5678},
      {"::1", 65534},
  };

  for (size_t i = 0; i < QUICHE_ARRAYSIZE(test_case); i++) {
    QuicIpAddress ip;
    ASSERT_TRUE(ip.FromString(test_case[i].ip_literal));
    QuicSocketAddressCoder encoder(QuicSocketAddress(ip, test_case[i].port));
    std::string serialized = encoder.Encode();

    QuicSocketAddressCoder decoder;
    ASSERT_TRUE(decoder.Decode(serialized.data(), serialized.length()));
    EXPECT_EQ(encoder.ip(), decoder.ip());
    EXPECT_EQ(encoder.port(), decoder.port());
  }
}

}  // namespace test
}  // namespace quic
