// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_ENCRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_ENCRYPTER_H_

#include "quiche/quic/core/crypto/chacha_base_encrypter.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// A ChaCha20Poly1305Encrypter is a QuicEncrypter that implements the
// AEAD_CHACHA20_POLY1305 algorithm specified in RFC 7539, except that
// it truncates the Poly1305 authenticator to 12 bytes. Create an instance
// by calling QuicEncrypter::Create(kCC20).
//
// It uses an authentication tag of 12 bytes (96 bits). The fixed prefix of the
// nonce is four bytes.
class QUICHE_EXPORT ChaCha20Poly1305Encrypter : public ChaChaBaseEncrypter {
 public:
  enum {
    kAuthTagSize = 12,
  };

  ChaCha20Poly1305Encrypter();
  ChaCha20Poly1305Encrypter(const ChaCha20Poly1305Encrypter&) = delete;
  ChaCha20Poly1305Encrypter& operator=(const ChaCha20Poly1305Encrypter&) =
      delete;
  ~ChaCha20Poly1305Encrypter() override;

  QuicPacketCount GetConfidentialityLimit() const override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_ENCRYPTER_H_
