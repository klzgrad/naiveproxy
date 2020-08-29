// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/jwk_serializer.h"

#include "base/base64url.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

// This is the ASN.1 prefix for a P-256 public key. Specifically it's:
// SEQUENCE
//   SEQUENCE
//     OID id-ecPublicKey
//     OID prime256v1
//   BIT STRING, length 66, 0 trailing bits: 0x04
//
// The 0x04 in the BIT STRING is the prefix for an uncompressed, X9.62
// public key. Following that are the two field elements as 32-byte,
// big-endian numbers, as required by the Channel ID.
static const unsigned char kP256SpkiPrefix[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
    0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04
};
static const unsigned int kEcCoordinateSize = 32U;

// This is a valid P-256 public key.
static const unsigned char kSpkiEc[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
    0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04,
    0x29, 0x5d, 0x6e, 0xfe, 0x33, 0x77, 0x26, 0xea,
    0x5b, 0xa4, 0xe6, 0x1b, 0x34, 0x6e, 0x7b, 0xa0,
    0xa3, 0x8f, 0x33, 0x49, 0xa0, 0x9c, 0xae, 0x98,
    0xbd, 0x46, 0x0d, 0xf6, 0xd4, 0x5a, 0xdc, 0x8a,
    0x1f, 0x8a, 0xb2, 0x20, 0x51, 0xb7, 0xd2, 0x87,
    0x0d, 0x53, 0x7e, 0x5d, 0x94, 0xa3, 0xe0, 0x34,
    0x16, 0xa1, 0xcc, 0x10, 0x48, 0xcd, 0x70, 0x9c,
    0x05, 0xd3, 0xd2, 0xca, 0xdf, 0x44, 0x2f, 0xf4
};

// This is a P-256 public key with a leading 0.
static const unsigned char kSpkiEcWithLeadingZero[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
    0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
    0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
    0x42, 0x00, 0x04,
    0x00, 0x8e, 0xd5, 0x49, 0x51, 0xad, 0x7d, 0xd3,
    0x3a, 0x59, 0x86, 0xd1, 0x2c, 0xba, 0x05, 0xd3,
    0xa6, 0x3c, 0x5d, 0x6d, 0x28, 0xde, 0x8f, 0xdd,
    0xa5, 0x3d, 0x30, 0x18, 0x05, 0x86, 0x76, 0x9c,
    0x7c, 0xa7, 0xba, 0x58, 0xea, 0x1a, 0x84, 0x19,
    0x29, 0x0a, 0x15, 0x30, 0x7d, 0x6b, 0x00, 0x41,
    0x64, 0x56, 0x84, 0x19, 0x54, 0x3e, 0x26, 0x13,
    0xc9, 0x1e, 0x31, 0x89, 0xe2, 0x62, 0xcb, 0x3f
};

// Section 2 of RFC 7515 defines that an URL-safe base64 encoding must be used
// with all trailing '=' characters omitted. Returns whether the |input|
// contains any of the forbidden base64 characters (+, -, =).
bool ContainsNonUrlSafeBase64Characters(base::StringPiece input) {
  return input.find_first_of("+-=") != std::string::npos;
}

}  // namespace

TEST(JwkSerializerTest, ConvertSpkiFromDerToJwkEc) {
  base::StringPiece spki;
  base::DictionaryValue public_key_jwk;

  EXPECT_FALSE(JwkSerializer::ConvertSpkiFromDerToJwk(spki, &public_key_jwk));
  EXPECT_TRUE(public_key_jwk.empty());

  // Test the result of a "normal" point on this curve.
  spki = base::StringPiece(reinterpret_cast<const char*>(kSpkiEc),
                           sizeof(kSpkiEc));
  EXPECT_TRUE(JwkSerializer::ConvertSpkiFromDerToJwk(spki, &public_key_jwk));

  std::string string_value;
  EXPECT_TRUE(public_key_jwk.GetString("kty", &string_value));
  EXPECT_STREQ("EC", string_value.c_str());
  EXPECT_TRUE(public_key_jwk.GetString("crv", &string_value));
  EXPECT_STREQ("P-256", string_value.c_str());

  EXPECT_TRUE(public_key_jwk.GetString("x", &string_value));
  EXPECT_FALSE(ContainsNonUrlSafeBase64Characters(string_value));
  std::string decoded_coordinate;
  EXPECT_TRUE(base::Base64UrlDecode(
      string_value, base::Base64UrlDecodePolicy::DISALLOW_PADDING,
      &decoded_coordinate));
  EXPECT_EQ(kEcCoordinateSize, decoded_coordinate.size());
  EXPECT_EQ(0,
            memcmp(decoded_coordinate.data(),
                   kSpkiEc + sizeof(kP256SpkiPrefix),
                   kEcCoordinateSize));

  EXPECT_TRUE(public_key_jwk.GetString("y", &string_value));
  EXPECT_FALSE(ContainsNonUrlSafeBase64Characters(string_value));
  EXPECT_TRUE(base::Base64UrlDecode(
      string_value, base::Base64UrlDecodePolicy::DISALLOW_PADDING,
      &decoded_coordinate));
  EXPECT_EQ(kEcCoordinateSize, decoded_coordinate.size());
  EXPECT_EQ(0,
            memcmp(decoded_coordinate.data(),
                  kSpkiEc + sizeof(kP256SpkiPrefix) + kEcCoordinateSize,
                  kEcCoordinateSize));

  // Test the result of a corner case: leading 0s in the x, y coordinates are
  // not trimmed, but the point is fixed-length encoded.
  spki = {reinterpret_cast<const char*>(kSpkiEcWithLeadingZero),
          sizeof(kSpkiEcWithLeadingZero)};
  EXPECT_TRUE(JwkSerializer::ConvertSpkiFromDerToJwk(spki, &public_key_jwk));

  EXPECT_TRUE(public_key_jwk.GetString("kty", &string_value));
  EXPECT_STREQ("EC", string_value.c_str());
  EXPECT_TRUE(public_key_jwk.GetString("crv", &string_value));
  EXPECT_STREQ("P-256", string_value.c_str());

  EXPECT_TRUE(public_key_jwk.GetString("x", &string_value));
  EXPECT_FALSE(ContainsNonUrlSafeBase64Characters(string_value));
  EXPECT_TRUE(base::Base64UrlDecode(
      string_value, base::Base64UrlDecodePolicy::DISALLOW_PADDING,
      &decoded_coordinate));
  EXPECT_EQ(kEcCoordinateSize, decoded_coordinate.size());
  EXPECT_EQ(0,
            memcmp(decoded_coordinate.data(),
                   kSpkiEcWithLeadingZero + sizeof(kP256SpkiPrefix),
                   kEcCoordinateSize));

  EXPECT_TRUE(public_key_jwk.GetString("y", &string_value));
  EXPECT_FALSE(ContainsNonUrlSafeBase64Characters(string_value));
  EXPECT_TRUE(base::Base64UrlDecode(
      string_value, base::Base64UrlDecodePolicy::DISALLOW_PADDING,
      &decoded_coordinate));
  EXPECT_EQ(kEcCoordinateSize, decoded_coordinate.size());
  EXPECT_EQ(0, memcmp(
      decoded_coordinate.data(),
      kSpkiEcWithLeadingZero + sizeof(kP256SpkiPrefix) + kEcCoordinateSize,
      kEcCoordinateSize));
}

}  // namespace net
