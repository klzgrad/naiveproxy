// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_AES_256_GCM_DECRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_AES_256_GCM_DECRYPTER_H_

#include <cstdint>

#include "quiche/quic/core/crypto/aes_base_decrypter.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// An Aes256GcmDecrypter is a QuicDecrypter that implements the
// AEAD_AES_256_GCM algorithm specified in RFC 5116 for use in IETF QUIC.
//
// It uses an authentication tag of 16 bytes (128 bits). It uses a 12 byte IV
// that is XOR'd with the packet number to compute the nonce.
class QUICHE_EXPORT Aes256GcmDecrypter : public AesBaseDecrypter {
 public:
  enum {
    kAuthTagSize = 16,
  };

  Aes256GcmDecrypter();
  Aes256GcmDecrypter(const Aes256GcmDecrypter&) = delete;
  Aes256GcmDecrypter& operator=(const Aes256GcmDecrypter&) = delete;
  ~Aes256GcmDecrypter() override;

  uint32_t cipher_id() const override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_AES_256_GCM_DECRYPTER_H_
