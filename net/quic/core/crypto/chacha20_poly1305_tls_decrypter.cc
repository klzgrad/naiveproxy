// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/chacha20_poly1305_tls_decrypter.h"

#include "net/quic/platform/api/quic_flag_utils.h"
#include "net/quic/platform/api/quic_flags.h"
#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/tls1.h"

namespace net {

namespace {

const size_t kKeySize = 32;
const size_t kNoncePrefixSize = 4;

}  // namespace

ChaCha20Poly1305TlsDecrypter::ChaCha20Poly1305TlsDecrypter()
    : AeadBaseDecrypter(EVP_aead_chacha20_poly1305(),
                        kKeySize,
                        kAuthTagSize,
                        kNoncePrefixSize,
                        /* use_ietf_nonce_construction */ true) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNoncePrefixSize <= kMaxNoncePrefixSize,
                "nonce prefix size too big");
}

ChaCha20Poly1305TlsDecrypter::~ChaCha20Poly1305TlsDecrypter() {}

uint32_t ChaCha20Poly1305TlsDecrypter::cipher_id() const {
  return TLS1_CK_CHACHA20_POLY1305_SHA256;
}

}  // namespace net
