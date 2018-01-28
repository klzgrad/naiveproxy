// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/aead_base_decrypter.h"

#include <cstdint>

#include "net/quic/core/quic_utils.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_logging.h"
#include "third_party/boringssl/src/include/openssl/err.h"
#include "third_party/boringssl/src/include/openssl/evp.h"

using std::string;

namespace net {

namespace {

// Clear OpenSSL error stack.
void ClearOpenSslErrors() {
  while (ERR_get_error()) {
  }
}

// In debug builds only, log OpenSSL error stack. Then clear OpenSSL error
// stack.
void DLogOpenSslErrors() {
#ifdef NDEBUG
  ClearOpenSslErrors();
#else
  while (uint32_t error = ERR_get_error()) {
    char buf[120];
    ERR_error_string_n(error, buf, arraysize(buf));
    QUIC_DLOG(ERROR) << "OpenSSL error: " << buf;
  }
#endif
}

}  // namespace

AeadBaseDecrypter::AeadBaseDecrypter(const EVP_AEAD* aead_alg,
                                     size_t key_size,
                                     size_t auth_tag_size,
                                     size_t nonce_prefix_size,
                                     bool use_ietf_nonce_construction)
    : aead_alg_(aead_alg),
      key_size_(key_size),
      auth_tag_size_(auth_tag_size),
      nonce_prefix_size_(nonce_prefix_size),
      use_ietf_nonce_construction_(use_ietf_nonce_construction),
      have_preliminary_key_(false) {
  DCHECK_GT(256u, key_size);
  DCHECK_GT(256u, auth_tag_size);
  DCHECK_GT(256u, nonce_prefix_size);
  DCHECK_LE(key_size_, sizeof(key_));
  DCHECK_LE(nonce_prefix_size_, sizeof(iv_));
}

AeadBaseDecrypter::~AeadBaseDecrypter() {}

bool AeadBaseDecrypter::SetKey(QuicStringPiece key) {
  DCHECK_EQ(key.size(), key_size_);
  if (key.size() != key_size_) {
    return false;
  }
  memcpy(key_, key.data(), key.size());

  EVP_AEAD_CTX_cleanup(ctx_.get());
  if (!EVP_AEAD_CTX_init(ctx_.get(), aead_alg_, key_, key_size_, auth_tag_size_,
                         nullptr)) {
    DLogOpenSslErrors();
    return false;
  }

  return true;
}

bool AeadBaseDecrypter::SetNoncePrefix(QuicStringPiece nonce_prefix) {
  if (use_ietf_nonce_construction_) {
    QUIC_BUG << "Attempted to set nonce prefix on IETF QUIC crypter";
    return false;
  }
  DCHECK_EQ(nonce_prefix.size(), nonce_prefix_size_);
  if (nonce_prefix.size() != nonce_prefix_size_) {
    return false;
  }
  memcpy(iv_, nonce_prefix.data(), nonce_prefix.size());
  return true;
}

bool AeadBaseDecrypter::SetIV(QuicStringPiece iv) {
  if (!use_ietf_nonce_construction_) {
    QUIC_BUG << "Attempted to set IV on Google QUIC crypter";
    return false;
  }
  DCHECK_EQ(iv.size(), nonce_prefix_size_ + sizeof(QuicPacketNumber));
  if (iv.size() != nonce_prefix_size_ + sizeof(QuicPacketNumber)) {
    return false;
  }
  memcpy(iv_, iv.data(), iv.size());
  return true;
}

bool AeadBaseDecrypter::SetPreliminaryKey(QuicStringPiece key) {
  DCHECK(!have_preliminary_key_);
  SetKey(key);
  have_preliminary_key_ = true;

  return true;
}

bool AeadBaseDecrypter::SetDiversificationNonce(
    const DiversificationNonce& nonce) {
  if (!have_preliminary_key_) {
    return true;
  }

  string key, nonce_prefix;
  DiversifyPreliminaryKey(
      QuicStringPiece(reinterpret_cast<const char*>(key_), key_size_),
      QuicStringPiece(reinterpret_cast<const char*>(iv_), nonce_prefix_size_),
      nonce, key_size_, nonce_prefix_size_, &key, &nonce_prefix);

  if (!SetKey(key) || !SetNoncePrefix(nonce_prefix)) {
    DCHECK(false);
    return false;
  }

  have_preliminary_key_ = false;
  return true;
}

bool AeadBaseDecrypter::DecryptPacket(QuicTransportVersion /*version*/,
                                      QuicPacketNumber packet_number,
                                      QuicStringPiece associated_data,
                                      QuicStringPiece ciphertext,
                                      char* output,
                                      size_t* output_length,
                                      size_t max_output_length) {
  if (ciphertext.length() < auth_tag_size_) {
    return false;
  }

  if (have_preliminary_key_) {
    QUIC_BUG << "Unable to decrypt while key diversification is pending";
    return false;
  }

  uint8_t nonce[kMaxIVSize];
  const size_t nonce_size = nonce_prefix_size_ + sizeof(packet_number);
  memcpy(nonce, iv_, nonce_size);
  if (use_ietf_nonce_construction_) {
    for (size_t i = 0; i < sizeof(packet_number); ++i) {
      nonce[nonce_prefix_size_ + i] ^=
          (packet_number >> ((sizeof(packet_number) - i + 1) * 8)) & 0xff;
    }
  } else {
    memcpy(nonce + nonce_prefix_size_, &packet_number, sizeof(packet_number));
  }
  if (!EVP_AEAD_CTX_open(
          ctx_.get(), reinterpret_cast<uint8_t*>(output), output_length,
          max_output_length, reinterpret_cast<const uint8_t*>(nonce),
          nonce_size, reinterpret_cast<const uint8_t*>(ciphertext.data()),
          ciphertext.size(),
          reinterpret_cast<const uint8_t*>(associated_data.data()),
          associated_data.size())) {
    // Because QuicFramer does trial decryption, decryption errors are expected
    // when encryption level changes. So we don't log decryption errors.
    ClearOpenSslErrors();
    return false;
  }
  return true;
}

QuicStringPiece AeadBaseDecrypter::GetKey() const {
  return QuicStringPiece(reinterpret_cast<const char*>(key_), key_size_);
}

QuicStringPiece AeadBaseDecrypter::GetNoncePrefix() const {
  if (nonce_prefix_size_ == 0) {
    return QuicStringPiece();
  }
  return QuicStringPiece(reinterpret_cast<const char*>(iv_),
                         nonce_prefix_size_);
}

}  // namespace net
