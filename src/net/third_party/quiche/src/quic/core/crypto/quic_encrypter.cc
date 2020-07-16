// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"

#include <utility>

#include "third_party/boringssl/src/include/openssl/tls1.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/aes_256_gcm_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_tls_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quiche/src/quic/core/crypto/null_encrypter.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"

namespace quic {

// static
std::unique_ptr<QuicEncrypter> QuicEncrypter::Create(
    const ParsedQuicVersion& version,
    QuicTag algorithm) {
  switch (algorithm) {
    case kAESG:
      if (version.UsesInitialObfuscators()) {
        return std::make_unique<Aes128GcmEncrypter>();
      } else {
        return std::make_unique<Aes128Gcm12Encrypter>();
      }
    case kCC20:
      if (version.UsesInitialObfuscators()) {
        return std::make_unique<ChaCha20Poly1305TlsEncrypter>();
      } else {
        return std::make_unique<ChaCha20Poly1305Encrypter>();
      }
    default:
      QUIC_LOG(FATAL) << "Unsupported algorithm: " << algorithm;
      return nullptr;
  }
}

// static
std::unique_ptr<QuicEncrypter> QuicEncrypter::CreateFromCipherSuite(
    uint32_t cipher_suite) {
  switch (cipher_suite) {
    case TLS1_CK_AES_128_GCM_SHA256:
      return std::make_unique<Aes128GcmEncrypter>();
    case TLS1_CK_AES_256_GCM_SHA384:
      return std::make_unique<Aes256GcmEncrypter>();
    case TLS1_CK_CHACHA20_POLY1305_SHA256:
      return std::make_unique<ChaCha20Poly1305TlsEncrypter>();
    default:
      QUIC_BUG << "TLS cipher suite is unknown to QUIC";
      return nullptr;
  }
}

}  // namespace quic
