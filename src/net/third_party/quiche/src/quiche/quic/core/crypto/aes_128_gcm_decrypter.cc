// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/aes_128_gcm_decrypter.h"

#include "openssl/aead.h"
#include "openssl/tls1.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

namespace {

const size_t kKeySize = 16;
const size_t kNonceSize = 12;

}  // namespace

Aes128GcmDecrypter::Aes128GcmDecrypter()
    : AesBaseDecrypter(EVP_aead_aes_128_gcm, kKeySize, kAuthTagSize, kNonceSize,
                       /* use_ietf_nonce_construction */ true) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

Aes128GcmDecrypter::~Aes128GcmDecrypter() {}

uint32_t Aes128GcmDecrypter::cipher_id() const {
  return TLS1_CK_AES_128_GCM_SHA256;
}

}  // namespace quic
