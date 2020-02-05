// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aes_128_gcm_12_decrypter.h"

#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/tls1.h"

namespace quic {

namespace {

const size_t kKeySize = 16;
const size_t kNonceSize = 12;

}  // namespace

Aes128Gcm12Decrypter::Aes128Gcm12Decrypter()
    : AesBaseDecrypter(EVP_aead_aes_128_gcm,
                       kKeySize,
                       kAuthTagSize,
                       kNonceSize,
                       /* use_ietf_nonce_construction */ false) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

Aes128Gcm12Decrypter::~Aes128Gcm12Decrypter() {}

uint32_t Aes128Gcm12Decrypter::cipher_id() const {
  return TLS1_CK_AES_128_GCM_SHA256;
}

}  // namespace quic
