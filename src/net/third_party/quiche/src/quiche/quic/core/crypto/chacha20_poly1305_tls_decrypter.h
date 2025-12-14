// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_TLS_DECRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_TLS_DECRYPTER_H_

#include <cstdint>

#include "quiche/quic/core/crypto/chacha_base_decrypter.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A ChaCha20Poly1305TlsDecrypter is a QuicDecrypter that implements the
// AEAD_CHACHA20_POLY1305 algorithm specified in RFC 7539 for use in IETF QUIC.
//
// It uses an authentication tag of 16 bytes (128 bits). It uses a 12 bytes IV
// that is XOR'd with the packet number to compute the nonce.
class QUICHE_EXPORT ChaCha20Poly1305TlsDecrypter : public ChaChaBaseDecrypter {
 public:
  enum {
    kAuthTagSize = 16,
  };

  ChaCha20Poly1305TlsDecrypter();
  ChaCha20Poly1305TlsDecrypter(const ChaCha20Poly1305TlsDecrypter&) = delete;
  ChaCha20Poly1305TlsDecrypter& operator=(const ChaCha20Poly1305TlsDecrypter&) =
      delete;
  ~ChaCha20Poly1305TlsDecrypter() override;

  uint32_t cipher_id() const override;
  QuicPacketCount GetIntegrityLimit() const override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_TLS_DECRYPTER_H_
