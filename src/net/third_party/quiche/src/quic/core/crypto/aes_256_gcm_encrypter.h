// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_AES_256_GCM_ENCRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_AES_256_GCM_ENCRYPTER_H_

#include "net/third_party/quiche/src/quic/core/crypto/aes_base_encrypter.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// An Aes256GcmEncrypter is a QuicEncrypter that implements the
// AEAD_AES_256_GCM algorithm specified in RFC 5116 for use in IETF QUIC.
//
// It uses an authentication tag of 16 bytes (128 bits). It uses a 12 byte IV
// that is XOR'd with the packet number to compute the nonce.
class QUIC_EXPORT_PRIVATE Aes256GcmEncrypter : public AesBaseEncrypter {
 public:
  enum {
    kAuthTagSize = 16,
  };

  Aes256GcmEncrypter();
  Aes256GcmEncrypter(const Aes256GcmEncrypter&) = delete;
  Aes256GcmEncrypter& operator=(const Aes256GcmEncrypter&) = delete;
  ~Aes256GcmEncrypter() override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_AES_256_GCM_ENCRYPTER_H_
