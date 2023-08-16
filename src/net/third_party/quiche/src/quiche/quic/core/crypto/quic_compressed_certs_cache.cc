// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"

#include <string>

namespace quic {

namespace {

// Inline helper function for extending a 64-bit |seed| in-place with a 64-bit
// |value|. Based on Boost's hash_combine function.
inline void hash_combine(uint64_t* seed, const uint64_t& val) {
  (*seed) ^= val + 0x9e3779b9 + ((*seed) << 6) + ((*seed) >> 2);
}

}  // namespace

const size_t QuicCompressedCertsCache::kQuicCompressedCertsCacheSize = 225;

QuicCompressedCertsCache::UncompressedCerts::UncompressedCerts()
    : chain(nullptr), client_cached_cert_hashes(nullptr) {}

QuicCompressedCertsCache::UncompressedCerts::UncompressedCerts(
    const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
    const std::string* client_cached_cert_hashes)
    : chain(chain), client_cached_cert_hashes(client_cached_cert_hashes) {}

QuicCompressedCertsCache::UncompressedCerts::~UncompressedCerts() {}

QuicCompressedCertsCache::CachedCerts::CachedCerts() {}

QuicCompressedCertsCache::CachedCerts::CachedCerts(
    const UncompressedCerts& uncompressed_certs,
    const std::string& compressed_cert)
    : chain_(uncompressed_certs.chain),
      client_cached_cert_hashes_(*uncompressed_certs.client_cached_cert_hashes),
      compressed_cert_(compressed_cert) {}

QuicCompressedCertsCache::CachedCerts::CachedCerts(const CachedCerts& other) =
    default;

QuicCompressedCertsCache::CachedCerts::~CachedCerts() {}

bool QuicCompressedCertsCache::CachedCerts::MatchesUncompressedCerts(
    const UncompressedCerts& uncompressed_certs) const {
  return (client_cached_cert_hashes_ ==
              *uncompressed_certs.client_cached_cert_hashes &&
          chain_ == uncompressed_certs.chain);
}

const std::string* QuicCompressedCertsCache::CachedCerts::compressed_cert()
    const {
  return &compressed_cert_;
}

QuicCompressedCertsCache::QuicCompressedCertsCache(int64_t max_num_certs)
    : certs_cache_(max_num_certs) {}

QuicCompressedCertsCache::~QuicCompressedCertsCache() {
  // Underlying cache must be cleared before destruction.
  certs_cache_.Clear();
}

const std::string* QuicCompressedCertsCache::GetCompressedCert(
    const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
    const std::string& client_cached_cert_hashes) {
  UncompressedCerts uncompressed_certs(chain, &client_cached_cert_hashes);

  uint64_t key = ComputeUncompressedCertsHash(uncompressed_certs);

  CachedCerts* cached_value = nullptr;
  auto iter = certs_cache_.Lookup(key);
  if (iter != certs_cache_.end()) {
    cached_value = iter->second.get();
  }
  if (cached_value != nullptr &&
      cached_value->MatchesUncompressedCerts(uncompressed_certs)) {
    return cached_value->compressed_cert();
  }
  return nullptr;
}

void QuicCompressedCertsCache::Insert(
    const quiche::QuicheReferenceCountedPointer<ProofSource::Chain>& chain,
    const std::string& client_cached_cert_hashes,
    const std::string& compressed_cert) {
  UncompressedCerts uncompressed_certs(chain, &client_cached_cert_hashes);

  uint64_t key = ComputeUncompressedCertsHash(uncompressed_certs);

  // Insert one unit to the cache.
  std::unique_ptr<CachedCerts> cached_certs(
      new CachedCerts(uncompressed_certs, compressed_cert));
  certs_cache_.Insert(key, std::move(cached_certs));
}

size_t QuicCompressedCertsCache::MaxSize() { return certs_cache_.MaxSize(); }

size_t QuicCompressedCertsCache::Size() { return certs_cache_.Size(); }

uint64_t QuicCompressedCertsCache::ComputeUncompressedCertsHash(
    const UncompressedCerts& uncompressed_certs) {
  uint64_t hash =
      std::hash<std::string>()(*uncompressed_certs.client_cached_cert_hashes);

  hash_combine(&hash,
               reinterpret_cast<uint64_t>(uncompressed_certs.chain.get()));
  return hash;
}

}  // namespace quic
