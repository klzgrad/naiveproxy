// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"

#include <string>
#include <utility>

#include "third_party/boringssl/src/include/openssl/tls1.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_256_gcm_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_tls_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_hkdf.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// static
std::unique_ptr<QuicDecrypter> QuicDecrypter::Create(
    const ParsedQuicVersion& version,
    QuicTag algorithm) {
  switch (algorithm) {
    case kAESG:
      if (version.UsesInitialObfuscators()) {
        return std::make_unique<Aes128GcmDecrypter>();
      } else {
        return std::make_unique<Aes128Gcm12Decrypter>();
      }
    case kCC20:
      if (version.UsesInitialObfuscators()) {
        return std::make_unique<ChaCha20Poly1305TlsDecrypter>();
      } else {
        return std::make_unique<ChaCha20Poly1305Decrypter>();
      }
    default:
      QUIC_LOG(FATAL) << "Unsupported algorithm: " << algorithm;
      return nullptr;
  }
}

// static
std::unique_ptr<QuicDecrypter> QuicDecrypter::CreateFromCipherSuite(
    uint32_t cipher_suite) {
  switch (cipher_suite) {
    case TLS1_CK_AES_128_GCM_SHA256:
      return std::make_unique<Aes128GcmDecrypter>();
    case TLS1_CK_AES_256_GCM_SHA384:
      return std::make_unique<Aes256GcmDecrypter>();
    case TLS1_CK_CHACHA20_POLY1305_SHA256:
      return std::make_unique<ChaCha20Poly1305TlsDecrypter>();
    default:
      QUIC_BUG << "TLS cipher suite is unknown to QUIC";
      return nullptr;
  }
}

// static
void QuicDecrypter::DiversifyPreliminaryKey(
    quiche::QuicheStringPiece preliminary_key,
    quiche::QuicheStringPiece nonce_prefix,
    const DiversificationNonce& nonce,
    size_t key_size,
    size_t nonce_prefix_size,
    std::string* out_key,
    std::string* out_nonce_prefix) {
  QuicHKDF hkdf((std::string(preliminary_key)) + (std::string(nonce_prefix)),
                quiche::QuicheStringPiece(nonce.data(), nonce.size()),
                "QUIC key diversification", 0, key_size, 0, nonce_prefix_size,
                0);
  *out_key = std::string(hkdf.server_write_key());
  *out_nonce_prefix = std::string(hkdf.server_write_iv());
}

}  // namespace quic
