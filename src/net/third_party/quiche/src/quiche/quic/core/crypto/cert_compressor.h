// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CERT_COMPRESSOR_H_
#define QUICHE_QUIC_CORE_CRYPTO_CERT_COMPRESSOR_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// CertCompressor provides functions for compressing and decompressing
// certificate chains using two techniquies:
//   1) The peer may provide a list of a 64-bit, FNV-1a hashes of certificates
//      that they already have. In the event that one of them is to be
//      compressed, it can be replaced with just the hash.
//   2) Otherwise the certificates are compressed with zlib using a pre-shared
//      dictionary that consists of the certificates handled with the above
//      methods and a small chunk of common substrings.
class QUIC_EXPORT_PRIVATE CertCompressor {
 public:
  CertCompressor() = delete;

  // CompressChain compresses the certificates in |certs| and returns a
  // compressed representation. client_cached_cert_hashes| contains
  // 64-bit, FNV-1a hashes of certificates that the peer already possesses.
  static std::string CompressChain(const std::vector<std::string>& certs,
                                   absl::string_view client_cached_cert_hashes);

  // DecompressChain decompresses the result of |CompressChain|, given in |in|,
  // into a series of certificates that are written to |out_certs|.
  // |cached_certs| contains certificates that the peer may have omitted.
  static bool DecompressChain(absl::string_view in,
                              const std::vector<std::string>& cached_certs,
                              std::vector<std::string>* out_certs);
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CERT_COMPRESSOR_H_
