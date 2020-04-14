// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/chacha20_poly1305_tls_encrypter.h"

#include "third_party/boringssl/src/include/openssl/evp.h"

namespace quic {

namespace {

const size_t kKeySize = 32;
const size_t kNonceSize = 12;

}  // namespace

ChaCha20Poly1305TlsEncrypter::ChaCha20Poly1305TlsEncrypter()
    : ChaChaBaseEncrypter(EVP_aead_chacha20_poly1305,
                          kKeySize,
                          kAuthTagSize,
                          kNonceSize,
                          /* use_ietf_nonce_construction */ true) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

ChaCha20Poly1305TlsEncrypter::~ChaCha20Poly1305TlsEncrypter() {}

}  // namespace quic
