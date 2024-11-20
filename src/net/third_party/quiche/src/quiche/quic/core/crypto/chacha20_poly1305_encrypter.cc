// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/chacha20_poly1305_encrypter.h"

#include <limits>

#include "openssl/evp.h"

namespace quic {

namespace {

const size_t kKeySize = 32;
const size_t kNonceSize = 12;

}  // namespace

ChaCha20Poly1305Encrypter::ChaCha20Poly1305Encrypter()
    : ChaChaBaseEncrypter(EVP_aead_chacha20_poly1305, kKeySize, kAuthTagSize,
                          kNonceSize,
                          /* use_ietf_nonce_construction */ false) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

ChaCha20Poly1305Encrypter::~ChaCha20Poly1305Encrypter() {}

QuicPacketCount ChaCha20Poly1305Encrypter::GetConfidentialityLimit() const {
  // For AEAD_CHACHA20_POLY1305, the confidentiality limit is greater than the
  // number of possible packets (2^62) and so can be disregarded.
  // https://quicwg.org/base-drafts/draft-ietf-quic-tls.html#name-limits-on-aead-usage
  return std::numeric_limits<QuicPacketCount>::max();
}

}  // namespace quic
