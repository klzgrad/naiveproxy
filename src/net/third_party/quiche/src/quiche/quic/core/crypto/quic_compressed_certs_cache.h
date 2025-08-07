// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_QUIC_COMPRESSED_CERTS_CACHE_H_
#define QUICHE_QUIC_CORE_CRYPTO_QUIC_COMPRESSED_CERTS_CACHE_H_

#include <string>
#include <vector>

#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_lru_cache.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// QuicCompressedCertsCache is a cache to track most recently compressed certs.
class QUICHE_EXPORT QuicCompressedCertsCache {
 public:
  explicit QuicCompressedCertsCache(int64_t max_num_certs);
  ~QuicCompressedCertsCache();

  // Returns the pointer to the cached compressed cert if
  // |chain, client_cached_cert_hashes| hits cache.
  // Otherwise, return nullptr.
  // Returned pointer might become invalid on the next call to Insert().
  const std::string* GetCompressedCert(
      const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
      const std::string& client_cached_cert_hashes);

  // Inserts the specified
  // |chain, client_cached_cert_hashes, compressed_cert| tuple to the cache.
  // If the insertion causes the cache to become overfull, entries will
  // be deleted in an LRU order to make room.
  void Insert(
      const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
      const std::string& client_cached_cert_hashes,
      const std::string& compressed_cert);

  // Returns max number of cache entries the cache can carry.
  size_t MaxSize();

  // Returns current number of cache entries in the cache.
  size_t Size();

  // Default size of the QuicCompressedCertsCache per server side investigation.
  static const size_t kQuicCompressedCertsCacheSize;

 private:
  // A wrapper of the tuple:
  //   |chain, client_cached_cert_hashes|
  // to identify uncompressed representation of certs.
  struct QUICHE_EXPORT UncompressedCerts {
    UncompressedCerts();
    UncompressedCerts(
        const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
        const std::string* client_cached_cert_hashes);
    ~UncompressedCerts();

    const quiche::QuicheReferenceCountedPointer<ProofSource::Chain> chain;
    const std::string* client_cached_cert_hashes;
  };

  // Certs stored by QuicCompressedCertsCache where uncompressed certs data is
  // used to identify the uncompressed representation of certs and
  // |compressed_cert| is the cached compressed representation.
  class QUICHE_EXPORT CachedCerts {
   public:
    CachedCerts();
    CachedCerts(const UncompressedCerts& uncompressed_certs,
                const std::string& compressed_cert);
    CachedCerts(const CachedCerts& other);
    ~CachedCerts();

    // Returns true if the |uncompressed_certs| matches uncompressed
    // representation of this cert.
    bool MatchesUncompressedCerts(
        const UncompressedCerts& uncompressed_certs) const;

    const std::string* compressed_cert() const;

   private:
    // Uncompressed certs data.
    quiche::QuicheReferenceCountedPointer<ProofSource::Chain> chain_;
    const std::string client_cached_cert_hashes_;

    // Cached compressed representation derived from uncompressed certs.
    const std::string compressed_cert_;
  };

  // Computes a uint64_t hash for |uncompressed_certs|.
  uint64_t ComputeUncompressedCertsHash(
      const UncompressedCerts& uncompressed_certs);

  // Key is a unit64_t hash for UncompressedCerts. Stored associated value is
  // CachedCerts which has both original uncompressed certs data and the
  // compressed representation of the certs.
  QuicLRUCache<uint64_t, CachedCerts> certs_cache_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_QUIC_COMPRESSED_CERTS_CACHE_H_
