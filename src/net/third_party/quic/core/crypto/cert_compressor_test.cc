// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/cert_compressor.h"

#include <memory>

#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/test_tools/crypto_test_utils.h"

namespace quic {
namespace test {

class CertCompressorTest : public QuicTest {};

TEST_F(CertCompressorTest, EmptyChain) {
  std::vector<QuicString> chain;
  const QuicString compressed = CertCompressor::CompressChain(
      chain, QuicStringPiece(), QuicStringPiece(), nullptr);
  EXPECT_EQ("00", QuicTextUtils::HexEncode(compressed));

  std::vector<QuicString> chain2, cached_certs;
  ASSERT_TRUE(CertCompressor::DecompressChain(compressed, cached_certs, nullptr,
                                              &chain2));
  EXPECT_EQ(chain.size(), chain2.size());
}

TEST_F(CertCompressorTest, Compressed) {
  std::vector<QuicString> chain;
  chain.push_back("testcert");
  const QuicString compressed = CertCompressor::CompressChain(
      chain, QuicStringPiece(), QuicStringPiece(), nullptr);
  ASSERT_GE(compressed.size(), 2u);
  EXPECT_EQ("0100", QuicTextUtils::HexEncode(compressed.substr(0, 2)));

  std::vector<QuicString> chain2, cached_certs;
  ASSERT_TRUE(CertCompressor::DecompressChain(compressed, cached_certs, nullptr,
                                              &chain2));
  EXPECT_EQ(chain.size(), chain2.size());
  EXPECT_EQ(chain[0], chain2[0]);
}

TEST_F(CertCompressorTest, Common) {
  std::vector<QuicString> chain;
  chain.push_back("testcert");
  static const uint64_t set_hash = 42;
  std::unique_ptr<CommonCertSets> common_sets(
      crypto_test_utils::MockCommonCertSets(chain[0], set_hash, 1));
  const QuicString compressed = CertCompressor::CompressChain(
      chain,
      QuicStringPiece(reinterpret_cast<const char*>(&set_hash),
                      sizeof(set_hash)),
      QuicStringPiece(), common_sets.get());
  EXPECT_EQ(
      "03"               /* common */
      "2a00000000000000" /* set hash 42 */
      "01000000"         /* index 1 */
      "00" /* end of list */,
      QuicTextUtils::HexEncode(compressed));

  std::vector<QuicString> chain2, cached_certs;
  ASSERT_TRUE(CertCompressor::DecompressChain(compressed, cached_certs,
                                              common_sets.get(), &chain2));
  EXPECT_EQ(chain.size(), chain2.size());
  EXPECT_EQ(chain[0], chain2[0]);
}

TEST_F(CertCompressorTest, Cached) {
  std::vector<QuicString> chain;
  chain.push_back("testcert");
  uint64_t hash = QuicUtils::FNV1a_64_Hash(chain[0]);
  QuicStringPiece hash_bytes(reinterpret_cast<char*>(&hash), sizeof(hash));
  const QuicString compressed = CertCompressor::CompressChain(
      chain, QuicStringPiece(), hash_bytes, nullptr);

  EXPECT_EQ("02" /* cached */ + QuicTextUtils::HexEncode(hash_bytes) +
                "00" /* end of list */,
            QuicTextUtils::HexEncode(compressed));

  std::vector<QuicString> cached_certs, chain2;
  cached_certs.push_back(chain[0]);
  ASSERT_TRUE(CertCompressor::DecompressChain(compressed, cached_certs, nullptr,
                                              &chain2));
  EXPECT_EQ(chain.size(), chain2.size());
  EXPECT_EQ(chain[0], chain2[0]);
}

TEST_F(CertCompressorTest, BadInputs) {
  std::vector<QuicString> cached_certs, chain;

  EXPECT_FALSE(CertCompressor::DecompressChain(
      QuicTextUtils::HexEncode("04") /* bad entry type */, cached_certs,
      nullptr, &chain));

  EXPECT_FALSE(CertCompressor::DecompressChain(
      QuicTextUtils::HexEncode("01") /* no terminator */, cached_certs, nullptr,
      &chain));

  EXPECT_FALSE(CertCompressor::DecompressChain(
      QuicTextUtils::HexEncode("0200") /* hash truncated */, cached_certs,
      nullptr, &chain));

  EXPECT_FALSE(CertCompressor::DecompressChain(
      QuicTextUtils::HexEncode("0300") /* hash and index truncated */,
      cached_certs, nullptr, &chain));

  /* without a CommonCertSets */
  EXPECT_FALSE(CertCompressor::DecompressChain(
      QuicTextUtils::HexEncode("03"
                               "0000000000000000"
                               "00000000"),
      cached_certs, nullptr, &chain));

  std::unique_ptr<CommonCertSets> common_sets(
      crypto_test_utils::MockCommonCertSets("foo", 42, 1));

  /* incorrect hash and index */
  EXPECT_FALSE(CertCompressor::DecompressChain(
      QuicTextUtils::HexEncode("03"
                               "a200000000000000"
                               "00000000"),
      cached_certs, nullptr, &chain));
}

}  // namespace test
}  // namespace quic
