// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/chacha20_poly1305_tls_decrypter.h"

#include "openssl/aead.h"
#include "openssl/tls1.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace quic {

namespace {

const size_t kKeySize = 32;
const size_t kNonceSize = 12;

}  // namespace

ChaCha20Poly1305TlsDecrypter::ChaCha20Poly1305TlsDecrypter()
    : ChaChaBaseDecrypter(EVP_aead_chacha20_poly1305, kKeySize, kAuthTagSize,
                          kNonceSize,
                          /* use_ietf_nonce_construction */ true) {
  static_assert(kKeySize <= kMaxKeySize, "key size too big");
  static_assert(kNonceSize <= kMaxNonceSize, "nonce size too big");
}

ChaCha20Poly1305TlsDecrypter::~ChaCha20Poly1305TlsDecrypter() {}

uint32_t ChaCha20Poly1305TlsDecrypter::cipher_id() const {
  return TLS1_CK_CHACHA20_POLY1305_SHA256;
}

QuicPacketCount ChaCha20Poly1305TlsDecrypter::GetIntegrityLimit() const {
  // For AEAD_CHACHA20_POLY1305, the integrity limit is 2^36 invalid packets.
  // https://quicwg.org/base-drafts/draft-ietf-quic-tls.html#name-limits-on-aead-usage
  static_assert(kMaxIncomingPacketSize < 16384,
                "This key limit requires limits on decryption payload sizes");
  return 68719476736U;
}

}  // namespace quic
