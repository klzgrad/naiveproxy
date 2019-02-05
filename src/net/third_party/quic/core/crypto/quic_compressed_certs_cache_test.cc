// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/quic_compressed_certs_cache.h"

#include "base/macros.h"
#include "net/third_party/quic/core/crypto/cert_compressor.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_test.h"
#include "net/third_party/quic/platform/api/quic_text_utils.h"
#include "net/third_party/quic/test_tools/crypto_test_utils.h"

namespace quic {

namespace test {

namespace {

class QuicCompressedCertsCacheTest : public testing::Test {
 public:
  QuicCompressedCertsCacheTest()
      : certs_cache_(QuicCompressedCertsCache::kQuicCompressedCertsCacheSize) {}

 protected:
  QuicCompressedCertsCache certs_cache_;
};

TEST_F(QuicCompressedCertsCacheTest, CacheHit) {
  std::vector<QuicString> certs = {"leaf cert", "intermediate cert",
                                   "root cert"};
  QuicReferenceCountedPointer<ProofSource::Chain> chain(
      new ProofSource::Chain(certs));
  QuicString common_certs = "common certs";
  QuicString cached_certs = "cached certs";
  QuicString compressed = "compressed cert";

  certs_cache_.Insert(chain, common_certs, cached_certs, compressed);

  const QuicString* cached_value =
      certs_cache_.GetCompressedCert(chain, common_certs, cached_certs);
  ASSERT_NE(nullptr, cached_value);
  EXPECT_EQ(*cached_value, compressed);
}

TEST_F(QuicCompressedCertsCacheTest, CacheMiss) {
  std::vector<QuicString> certs = {"leaf cert", "intermediate cert",
                                   "root cert"};
  QuicReferenceCountedPointer<ProofSource::Chain> chain(
      new ProofSource::Chain(certs));

  QuicString common_certs = "common certs";
  QuicString cached_certs = "cached certs";
  QuicString compressed = "compressed cert";

  certs_cache_.Insert(chain, common_certs, cached_certs, compressed);

  EXPECT_EQ(nullptr, certs_cache_.GetCompressedCert(
                         chain, "mismatched common certs", cached_certs));
  EXPECT_EQ(nullptr, certs_cache_.GetCompressedCert(chain, common_certs,
                                                    "mismatched cached certs"));

  // A different chain though with equivalent certs should get a cache miss.
  QuicReferenceCountedPointer<ProofSource::Chain> chain2(
      new ProofSource::Chain(certs));
  EXPECT_EQ(nullptr,
            certs_cache_.GetCompressedCert(chain2, common_certs, cached_certs));
}

TEST_F(QuicCompressedCertsCacheTest, CacheMissDueToEviction) {
  // Test cache returns a miss when a queried uncompressed certs was cached but
  // then evicted.
  std::vector<QuicString> certs = {"leaf cert", "intermediate cert",
                                   "root cert"};
  QuicReferenceCountedPointer<ProofSource::Chain> chain(
      new ProofSource::Chain(certs));

  QuicString common_certs = "common certs";
  QuicString cached_certs = "cached certs";
  QuicString compressed = "compressed cert";
  certs_cache_.Insert(chain, common_certs, cached_certs, compressed);

  // Insert another kQuicCompressedCertsCacheSize certs to evict the first
  // cached cert.
  for (unsigned int i = 0;
       i < QuicCompressedCertsCache::kQuicCompressedCertsCacheSize; i++) {
    EXPECT_EQ(certs_cache_.Size(), i + 1);
    certs_cache_.Insert(chain, QuicTextUtils::Uint64ToString(i), "",
                        QuicTextUtils::Uint64ToString(i));
  }
  EXPECT_EQ(certs_cache_.MaxSize(), certs_cache_.Size());

  EXPECT_EQ(nullptr,
            certs_cache_.GetCompressedCert(chain, common_certs, cached_certs));
}

}  // namespace
}  // namespace test
}  // namespace quic
