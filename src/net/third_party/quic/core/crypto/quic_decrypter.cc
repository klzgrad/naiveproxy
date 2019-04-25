// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/crypto/quic_decrypter.h"

#include "net/third_party/quic/core/crypto/aes_128_gcm_12_decrypter.h"
#include "net/third_party/quic/core/crypto/aes_128_gcm_decrypter.h"
#include "net/third_party/quic/core/crypto/aes_256_gcm_decrypter.h"
#include "net/third_party/quic/core/crypto/chacha20_poly1305_decrypter.h"
#include "net/third_party/quic/core/crypto/chacha20_poly1305_tls_decrypter.h"
#include "net/third_party/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quic/core/crypto/null_decrypter.h"
#include "net/third_party/quic/core/crypto/quic_hkdf.h"
#include "net/third_party/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "third_party/boringssl/src/include/openssl/tls1.h"

namespace quic {

// static
std::unique_ptr<QuicDecrypter> QuicDecrypter::Create(QuicTag algorithm) {
  switch (algorithm) {
    case kAESG:
      return QuicMakeUnique<Aes128Gcm12Decrypter>();
    case kCC20:
      return QuicMakeUnique<ChaCha20Poly1305Decrypter>();
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
      return QuicMakeUnique<Aes128GcmDecrypter>();
    case TLS1_CK_AES_256_GCM_SHA384:
      return QuicMakeUnique<Aes256GcmDecrypter>();
    case TLS1_CK_CHACHA20_POLY1305_SHA256:
      return QuicMakeUnique<ChaCha20Poly1305TlsDecrypter>();
    default:
      QUIC_BUG << "TLS cipher suite is unknown to QUIC";
      return nullptr;
  }
}

// static
void QuicDecrypter::DiversifyPreliminaryKey(QuicStringPiece preliminary_key,
                                            QuicStringPiece nonce_prefix,
                                            const DiversificationNonce& nonce,
                                            size_t key_size,
                                            size_t nonce_prefix_size,
                                            QuicString* out_key,
                                            QuicString* out_nonce_prefix) {
  QuicHKDF hkdf((QuicString(preliminary_key)) + (QuicString(nonce_prefix)),
                QuicStringPiece(nonce.data(), nonce.size()),
                "QUIC key diversification", 0, key_size, 0, nonce_prefix_size,
                0);
  *out_key = QuicString(hkdf.server_write_key());
  *out_nonce_prefix = QuicString(hkdf.server_write_iv());
}

}  // namespace quic
