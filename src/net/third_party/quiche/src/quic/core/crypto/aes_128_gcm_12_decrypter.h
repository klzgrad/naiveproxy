// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_12_DECRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_12_DECRYPTER_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/crypto/aes_base_decrypter.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// An Aes128Gcm12Decrypter is a QuicDecrypter that implements the
// AEAD_AES_128_GCM_12 algorithm specified in RFC 5282. Create an instance by
// calling QuicDecrypter::Create(kAESG).
//
// It uses an authentication tag of 12 bytes (96 bits). The fixed prefix
// of the nonce is four bytes.
class QUIC_EXPORT_PRIVATE Aes128Gcm12Decrypter : public AesBaseDecrypter {
 public:
  enum {
    // Authentication tags are truncated to 96 bits.
    kAuthTagSize = 12,
  };

  Aes128Gcm12Decrypter();
  Aes128Gcm12Decrypter(const Aes128Gcm12Decrypter&) = delete;
  Aes128Gcm12Decrypter& operator=(const Aes128Gcm12Decrypter&) = delete;
  ~Aes128Gcm12Decrypter() override;

  uint32_t cipher_id() const override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_AES_128_GCM_12_DECRYPTER_H_
