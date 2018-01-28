// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/aes_256_gcm_encrypter.h"

#include "third_party/boringssl/src/include/openssl/evp.h"

namespace net {

namespace {

const size_t kKeySize = 32;
const size_t kNoncePrefixSize = 4;

}  // namespace

Aes256GcmEncrypter::Aes256GcmEncrypter()
    : AeadBaseEncrypter(EVP_aead_aes_256_gcm(),
                        kKeySize,
                        kAuthTagSize,
                        kNoncePrefixSize,
                        /* use_ietf_nonce_construction */ true) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNoncePrefixSize <= kMaxNoncePrefixSize,
                "nonce prefix size too big");
}

Aes256GcmEncrypter::~Aes256GcmEncrypter() {}

}  // namespace net
