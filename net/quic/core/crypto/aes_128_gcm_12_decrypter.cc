// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/aes_128_gcm_12_decrypter.h"

#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/tls1.h"

namespace net {

namespace {

const size_t kKeySize = 16;
const size_t kNonceSize = 12;

}  // namespace

Aes128Gcm12Decrypter::Aes128Gcm12Decrypter()
    : AeadBaseDecrypter(EVP_aead_aes_128_gcm(),
                        kKeySize,
                        kAuthTagSize,
                        kNonceSize,
                        /* use_ietf_nonce_construction */ false) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

Aes128Gcm12Decrypter::~Aes128Gcm12Decrypter() {}

uint32_t Aes128Gcm12Decrypter::cipher_id() const {
  if (FLAGS_quic_reloadable_flag_quic_use_tls13_cipher_suites) {
    QUIC_FLAG_COUNT(quic_reloadable_flag_quic_use_tls13_cipher_suites);
    return TLS1_CK_AES_128_GCM_SHA256;
  }
  return TLS1_CK_ECDHE_RSA_WITH_AES_128_GCM_SHA256;
}

}  // namespace net
