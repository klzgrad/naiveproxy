// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aes_256_gcm_decrypter.h"

#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/tls1.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"

namespace quic {

namespace {

const size_t kKeySize = 32;
const size_t kNonceSize = 12;

}  // namespace

Aes256GcmDecrypter::Aes256GcmDecrypter()
    : AesBaseDecrypter(EVP_aead_aes_256_gcm,
                       kKeySize,
                       kAuthTagSize,
                       kNonceSize,
                       /* use_ietf_nonce_construction */ true) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

Aes256GcmDecrypter::~Aes256GcmDecrypter() {}

uint32_t Aes256GcmDecrypter::cipher_id() const {
  return TLS1_CK_AES_256_GCM_SHA384;
}

}  // namespace quic
