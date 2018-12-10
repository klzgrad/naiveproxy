// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_TLS_ENCRYPTER_H_
#define NET_THIRD_PARTY_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_TLS_ENCRYPTER_H_

#include "base/macros.h"
#include "net/third_party/quic/core/crypto/aead_base_encrypter.h"
#include "net/third_party/quic/platform/api/quic_export.h"

namespace quic {

// A ChaCha20Poly1305Encrypter is a QuicEncrypter that implements the
// AEAD_CHACHA20_POLY1305 algorithm specified in RFC 7539 for use in IETF QUIC.
//
// It uses an authentication tag of 16 bytes (128 bits). It uses a 12 byte IV
// that is XOR'd with the packet number to compute the nonce.
class QUIC_EXPORT_PRIVATE ChaCha20Poly1305TlsEncrypter
    : public AeadBaseEncrypter {
 public:
  enum {
    kAuthTagSize = 16,
  };

  ChaCha20Poly1305TlsEncrypter();
  ChaCha20Poly1305TlsEncrypter(const ChaCha20Poly1305TlsEncrypter&) = delete;
  ChaCha20Poly1305TlsEncrypter& operator=(const ChaCha20Poly1305TlsEncrypter&) =
      delete;
  ~ChaCha20Poly1305TlsEncrypter() override;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_TLS_ENCRYPTER_H_
