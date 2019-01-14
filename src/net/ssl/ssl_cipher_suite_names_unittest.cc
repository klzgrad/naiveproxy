// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_cipher_suite_names.h"

#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "net/ssl/ssl_connection_status_flags.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

int kObsoleteVersion = SSL_CONNECTION_VERSION_TLS1;
int kModernVersion = SSL_CONNECTION_VERSION_TLS1_2;

uint16_t kModernCipherSuite =
    0xc02f; /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */

uint16_t kObsoleteCipherObsoleteKeyExchange =
    0x2f; /* TLS_RSA_WITH_AES_128_CBC_SHA */
uint16_t kObsoleteCipherModernKeyExchange =
    0xc014; /* TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA */
uint16_t kModernCipherObsoleteKeyExchange =
    0x9c; /* TLS_RSA_WITH_AES_128_GCM_SHA256 */
uint16_t kModernCipherModernKeyExchange =
    0xc02f; /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */

int MakeConnectionStatus(int version, uint16_t cipher_suite) {
  int connection_status = 0;

  SSLConnectionStatusSetVersion(version, &connection_status);
  SSLConnectionStatusSetCipherSuite(cipher_suite, &connection_status);

  return connection_status;
}

TEST(CipherSuiteNamesTest, Basic) {
  const char *key_exchange, *cipher, *mac;
  bool is_aead, is_tls13;

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0x000a);
  EXPECT_STREQ("RSA", key_exchange);
  EXPECT_STREQ("3DES_EDE_CBC", cipher);
  EXPECT_STREQ("HMAC-SHA1", mac);
  EXPECT_FALSE(is_aead);
  EXPECT_FALSE(is_tls13);

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0x002f);
  EXPECT_STREQ("RSA", key_exchange);
  EXPECT_STREQ("AES_128_CBC", cipher);
  EXPECT_STREQ("HMAC-SHA1", mac);
  EXPECT_FALSE(is_aead);
  EXPECT_FALSE(is_tls13);

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0xc030);
  EXPECT_STREQ("ECDHE_RSA", key_exchange);
  EXPECT_STREQ("AES_256_GCM", cipher);
  EXPECT_TRUE(is_aead);
  EXPECT_FALSE(is_tls13);
  EXPECT_EQ(nullptr, mac);

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0xcca9);
  EXPECT_STREQ("ECDHE_ECDSA", key_exchange);
  EXPECT_STREQ("CHACHA20_POLY1305", cipher);
  EXPECT_TRUE(is_aead);
  EXPECT_FALSE(is_tls13);
  EXPECT_EQ(nullptr, mac);

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0xff31);
  EXPECT_STREQ("???", key_exchange);
  EXPECT_STREQ("???", cipher);
  EXPECT_STREQ("???", mac);
  EXPECT_FALSE(is_aead);
  EXPECT_FALSE(is_tls13);

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0x1301);
  EXPECT_STREQ("AES_128_GCM", cipher);
  EXPECT_TRUE(is_aead);
  EXPECT_TRUE(is_tls13);
  EXPECT_EQ(nullptr, mac);
  EXPECT_EQ(nullptr, key_exchange);

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0x1302);
  EXPECT_STREQ("AES_256_GCM", cipher);
  EXPECT_TRUE(is_aead);
  EXPECT_TRUE(is_tls13);
  EXPECT_EQ(nullptr, mac);
  EXPECT_EQ(nullptr, key_exchange);

  SSLCipherSuiteToStrings(&key_exchange, &cipher, &mac, &is_aead, &is_tls13,
                          0x1303);
  EXPECT_STREQ("CHACHA20_POLY1305", cipher);
  EXPECT_TRUE(is_aead);
  EXPECT_TRUE(is_tls13);
  EXPECT_EQ(nullptr, mac);
  EXPECT_EQ(nullptr, key_exchange);
}

TEST(CipherSuiteNamesTest, ParseSSLCipherString) {
  uint16_t cipher_suite = 0;
  EXPECT_TRUE(ParseSSLCipherString("0x0004", &cipher_suite));
  EXPECT_EQ(0x00004u, cipher_suite);

  EXPECT_TRUE(ParseSSLCipherString("0xBEEF", &cipher_suite));
  EXPECT_EQ(0xBEEFu, cipher_suite);
}

TEST(CipherSuiteNamesTest, ParseSSLCipherStringFails) {
  const char* const cipher_strings[] = {
    "0004",
    "0x004",
    "0xBEEFY",
  };

  for (size_t i = 0; i < arraysize(cipher_strings); ++i) {
    uint16_t cipher_suite = 0;
    EXPECT_FALSE(ParseSSLCipherString(cipher_strings[i], &cipher_suite));
  }
}

TEST(CipherSuiteNamesTest, ObsoleteSSLStatusProtocol) {
  // Obsolete
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL,
            ObsoleteSSLStatus(MakeConnectionStatus(SSL_CONNECTION_VERSION_SSL2,
                                                   kModernCipherSuite)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL,
            ObsoleteSSLStatus(MakeConnectionStatus(SSL_CONNECTION_VERSION_SSL3,
                                                   kModernCipherSuite)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL,
            ObsoleteSSLStatus(MakeConnectionStatus(SSL_CONNECTION_VERSION_TLS1,
                                                   kModernCipherSuite)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL,
            ObsoleteSSLStatus(MakeConnectionStatus(
                SSL_CONNECTION_VERSION_TLS1_1, kModernCipherSuite)));

  // Modern
  EXPECT_EQ(OBSOLETE_SSL_NONE,
            ObsoleteSSLStatus(MakeConnectionStatus(
                SSL_CONNECTION_VERSION_TLS1_2, kModernCipherSuite)));
  EXPECT_EQ(OBSOLETE_SSL_NONE,
            ObsoleteSSLStatus(MakeConnectionStatus(SSL_CONNECTION_VERSION_QUIC,
                                                   kModernCipherSuite)));
}

TEST(CipherSuiteNamesTest, ObsoleteSSLStatusProtocolAndCipherSuite) {
  // Cartesian combos
  // As above, some of these combinations can't happen in practice.
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL | OBSOLETE_SSL_MASK_KEY_EXCHANGE |
                OBSOLETE_SSL_MASK_CIPHER,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kObsoleteVersion, kObsoleteCipherObsoleteKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL | OBSOLETE_SSL_MASK_KEY_EXCHANGE,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kObsoleteVersion, kModernCipherObsoleteKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL | OBSOLETE_SSL_MASK_CIPHER,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kObsoleteVersion, kObsoleteCipherModernKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_PROTOCOL,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kObsoleteVersion, kModernCipherModernKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_KEY_EXCHANGE | OBSOLETE_SSL_MASK_CIPHER,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kModernVersion, kObsoleteCipherObsoleteKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_KEY_EXCHANGE,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kModernVersion, kModernCipherObsoleteKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_MASK_CIPHER,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kModernVersion, kObsoleteCipherModernKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_NONE,
            ObsoleteSSLStatus(MakeConnectionStatus(
                kModernVersion, kModernCipherModernKeyExchange)));
  EXPECT_EQ(OBSOLETE_SSL_NONE, ObsoleteSSLStatus(MakeConnectionStatus(
                                   SSL_CONNECTION_VERSION_TLS1_3,
                                   0x1301 /* AES_128_GCM_SHA256 */)));
}

TEST(CipherSuiteNamesTest, HTTP2CipherSuites) {
  // Picked some random cipher suites.
  EXPECT_FALSE(
      IsTLSCipherSuiteAllowedByHTTP2(0x0 /* TLS_NULL_WITH_NULL_NULL */));
  EXPECT_FALSE(IsTLSCipherSuiteAllowedByHTTP2(
      0xc014 /* TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA */));
  EXPECT_FALSE(IsTLSCipherSuiteAllowedByHTTP2(
      0x9c /* TLS_RSA_WITH_AES_128_GCM_SHA256 */));

  // Non-existent cipher suite.
  EXPECT_FALSE(IsTLSCipherSuiteAllowedByHTTP2(0xffff)) << "Doesn't exist!";

  // HTTP/2-compatible ones.
  EXPECT_TRUE(IsTLSCipherSuiteAllowedByHTTP2(
      0xc02f /* TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 */));
  EXPECT_TRUE(IsTLSCipherSuiteAllowedByHTTP2(
      0xcca8 /* ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256 */));
  EXPECT_TRUE(IsTLSCipherSuiteAllowedByHTTP2(
      0xcca9 /* ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256 */));
  EXPECT_TRUE(IsTLSCipherSuiteAllowedByHTTP2(0x1301 /* AES_128_GCM_SHA256 */));
  EXPECT_TRUE(IsTLSCipherSuiteAllowedByHTTP2(0x1302 /* AES_256_GCM_SHA384 */));
  EXPECT_TRUE(IsTLSCipherSuiteAllowedByHTTP2(0x1303 /* CHACHA20_POLY1305 */));
}

}  // anonymous namespace

}  // namespace net
