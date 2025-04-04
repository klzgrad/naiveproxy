// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/aes_128_gcm_12_encrypter.h"

#include "openssl/evp.h"

namespace quic {

namespace {

const size_t kKeySize = 16;
const size_t kNonceSize = 12;

}  // namespace

Aes128Gcm12Encrypter::Aes128Gcm12Encrypter()
    : AesBaseEncrypter(EVP_aead_aes_128_gcm, kKeySize, kAuthTagSize, kNonceSize,
                       /* use_ietf_nonce_construction */ false) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

Aes128Gcm12Encrypter::~Aes128Gcm12Encrypter() {}

}  // namespace quic
