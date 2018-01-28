// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_AES_128_GCM_DECRYPTER_H_
#define NET_QUIC_CORE_CRYPTO_AES_128_GCM_DECRYPTER_H_

#include <cstdint>

#include "base/macros.h"
#include "net/quic/core/crypto/aead_base_decrypter.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

// An Aes128GcmDecrypter is a QuicDecrypter that implements the
// AEAD_AES_128_GCM algorithm specified in RFC 5116 for use in IETF QUIC.
//
// It uses an authentication tag of 16 bytes (128 bits). It uses a 12 byte IV
// that is XOR'd with the packet number to compute the nonce.
class QUIC_EXPORT_PRIVATE Aes128GcmDecrypter : public AeadBaseDecrypter {
 public:
  enum {
    kAuthTagSize = 16,
  };

  Aes128GcmDecrypter();
  ~Aes128GcmDecrypter() override;

  uint32_t cipher_id() const override;

 private:
  DISALLOW_COPY_AND_ASSIGN(Aes128GcmDecrypter);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_AES_128_GCM_DECRYPTER_H_
