// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_12_ENCRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_12_ENCRYPTER_H_

#include "quiche/quic/core/crypto/aes_base_encrypter.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// An Aes128Gcm12Encrypter is a QuicEncrypter that implements the
// AEAD_AES_128_GCM_12 algorithm specified in RFC 5282. Create an instance by
// calling QuicEncrypter::Create(kAESG).
//
// It uses an authentication tag of 12 bytes (96 bits). The fixed prefix
// of the nonce is four bytes.
class QUICHE_EXPORT Aes128Gcm12Encrypter : public AesBaseEncrypter {
 public:
  enum {
    // Authentication tags are truncated to 96 bits.
    kAuthTagSize = 12,
  };

  Aes128Gcm12Encrypter();
  Aes128Gcm12Encrypter(const Aes128Gcm12Encrypter&) = delete;
  Aes128Gcm12Encrypter& operator=(const Aes128Gcm12Encrypter&) = delete;
  ~Aes128Gcm12Encrypter() override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_12_ENCRYPTER_H_
