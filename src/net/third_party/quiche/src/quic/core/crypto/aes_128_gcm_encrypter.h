// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_ENCRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_ENCRYPTER_H_

#include "net/third_party/quiche/src/quic/core/crypto/aes_base_encrypter.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// An Aes128GcmEncrypter is a QuicEncrypter that implements the
// AEAD_AES_128_GCM algorithm specified in RFC 5116 for use in IETF QUIC.
//
// It uses an authentication tag of 16 bytes (128 bits). It uses a 12 byte IV
// that is XOR'd with the packet number to compute the nonce.
class QUIC_EXPORT_PRIVATE Aes128GcmEncrypter : public AesBaseEncrypter {
 public:
  enum {
    kAuthTagSize = 16,
  };

  Aes128GcmEncrypter();
  Aes128GcmEncrypter(const Aes128GcmEncrypter&) = delete;
  Aes128GcmEncrypter& operator=(const Aes128GcmEncrypter&) = delete;
  ~Aes128GcmEncrypter() override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_ENCRYPTER_H_
