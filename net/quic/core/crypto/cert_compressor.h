// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_CERT_COMPRESSOR_H_
#define NET_QUIC_CORE_CRYPTO_CERT_COMPRESSOR_H_

#include <string>
#include <vector>

#include "base/macros.h"
#include "net/quic/core/crypto/common_cert_set.h"
#include "net/quic/core/crypto/crypto_protocol.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

// CertCompressor provides functions for compressing and decompressing
// certificate chains using three techniquies:
//   1) The peer may provide a list of a 64-bit, FNV-1a hashes of certificates
//      that they already have. In the event that one of them is to be
//      compressed, it can be replaced with just the hash.
//   2) The peer may provide a number of hashes that represent sets of
//      pre-shared certificates (CommonCertSets). If one of those certificates
//      is to be compressed, and it's known to the given CommonCertSets, then it
//      can be replaced with a set hash and certificate index.
//   3) Otherwise the certificates are compressed with zlib using a pre-shared
//      dictionary that consists of the certificates handled with the above
//      methods and a small chunk of common substrings.
class QUIC_EXPORT_PRIVATE CertCompressor {
 public:
  // CompressChain compresses the certificates in |certs| and returns a
  // compressed representation. |common_sets| contains the common certificate
  // sets known locally and |client_common_set_hashes| contains the hashes of
  // the common sets known to the peer. |client_cached_cert_hashes| contains
  // 64-bit, FNV-1a hashes of certificates that the peer already possesses.
  static std::string CompressChain(const std::vector<std::string>& certs,
                                   QuicStringPiece client_common_set_hashes,
                                   QuicStringPiece client_cached_cert_hashes,
                                   const CommonCertSets* common_sets);

  // DecompressChain decompresses the result of |CompressChain|, given in |in|,
  // into a series of certificates that are written to |out_certs|.
  // |cached_certs| contains certificates that the peer may have omitted and
  // |common_sets| contains the common certificate sets known locally.
  static bool DecompressChain(QuicStringPiece in,
                              const std::vector<std::string>& cached_certs,
                              const CommonCertSets* common_sets,
                              std::vector<std::string>* out_certs);

 private:
  DISALLOW_COPY_AND_ASSIGN(CertCompressor);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_CERT_COMPRESSOR_H_
