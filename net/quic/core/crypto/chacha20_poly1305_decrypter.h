// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_DECRYPTER_H_
#define NET_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_DECRYPTER_H_

#include <cstdint>

#include "base/macros.h"
#include "net/quic/core/crypto/aead_base_decrypter.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// A ChaCha20Poly1305Decrypter is a QuicDecrypter that implements the
// AEAD_CHACHA20_POLY1305 algorithm specified in RFC 7539, except that
// it truncates the Poly1305 authenticator to 12 bytes. Create an instance
// by calling QuicDecrypter::Create(kCC20).
//
// It uses an authentication tag of 12 bytes (96 bits). The fixed prefix of the
// nonce is four bytes.
class QUIC_EXPORT_PRIVATE ChaCha20Poly1305Decrypter : public AeadBaseDecrypter {
 public:
  enum {
    kAuthTagSize = 12,
  };

  ChaCha20Poly1305Decrypter();
  ~ChaCha20Poly1305Decrypter() override;

  uint32_t cipher_id() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(ChaCha20Poly1305Decrypter);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_CHACHA20_POLY1305_DECRYPTER_H_
