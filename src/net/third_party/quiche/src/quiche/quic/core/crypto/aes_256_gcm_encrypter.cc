// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/aes_256_gcm_encrypter.h"

#include "openssl/evp.h"

namespace quic {

namespace {

const size_t kKeySize = 32;
const size_t kNonceSize = 12;

}  // namespace

Aes256GcmEncrypter::Aes256GcmEncrypter()
    : AesBaseEncrypter(EVP_aead_aes_256_gcm, kKeySize, kAuthTagSize, kNonceSize,
                       /* use_ietf_nonce_construction */ true) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

Aes256GcmEncrypter::~Aes256GcmEncrypter() {}

}  // namespace quic
