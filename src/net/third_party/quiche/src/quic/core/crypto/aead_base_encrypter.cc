// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/aead_base_encrypter.h"

#include "third_party/boringssl/src/include/openssl/crypto.h"
#include "third_party/boringssl/src/include/openssl/err.h"
#include "third_party/boringssl/src/include/openssl/evp.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_aligned.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

// In debug builds only, log OpenSSL error stack. Then clear OpenSSL error
// stack.
void DLogOpenSslErrors() {
#ifdef NDEBUG
  while (ERR_get_error()) {
  }
#else
  while (unsigned long error = ERR_get_error()) {
    char buf[120];
    ERR_error_string_n(error, buf, QUICHE_ARRAYSIZE(buf));
    QUIC_DLOG(ERROR) << "OpenSSL error: " << buf;
  }
#endif
}

const EVP_AEAD* InitAndCall(const EVP_AEAD* (*aead_getter)()) {
  // Ensure BoringSSL is initialized before calling |aead_getter|. In Chromium,
  // the static initializer is disabled.
  CRYPTO_library_init();
  return aead_getter();
}

}  // namespace

AeadBaseEncrypter::AeadBaseEncrypter(const EVP_AEAD* (*aead_getter)(),
                                     size_t key_size,
                                     size_t auth_tag_size,
                                     size_t nonce_size,
                                     bool use_ietf_nonce_construction)
    : aead_alg_(InitAndCall(aead_getter)),
      key_size_(key_size),
      auth_tag_size_(auth_tag_size),
      nonce_size_(nonce_size),
      use_ietf_nonce_construction_(use_ietf_nonce_construction) {
  DCHECK_LE(key_size_, sizeof(key_));
  DCHECK_LE(nonce_size_, sizeof(iv_));
  DCHECK_GE(kMaxNonceSize, nonce_size_);
}

AeadBaseEncrypter::~AeadBaseEncrypter() {}

bool AeadBaseEncrypter::SetKey(quiche::QuicheStringPiece key) {
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

bool AeadBaseEncrypter::SetNoncePrefix(quiche::QuicheStringPiece nonce_prefix) {
  if (use_ietf_nonce_construction_) {
    QUIC_BUG << "Attempted to set nonce prefix on IETF QUIC crypter";
    return false;
  }
  DCHECK_EQ(nonce_prefix.size(), nonce_size_ - sizeof(QuicPacketNumber));
  if (nonce_prefix.size() != nonce_size_ - sizeof(QuicPacketNumber)) {
    return false;
  }
  memcpy(iv_, nonce_prefix.data(), nonce_prefix.size());
  return true;
}

bool AeadBaseEncrypter::SetIV(quiche::QuicheStringPiece iv) {
  if (!use_ietf_nonce_construction_) {
    QUIC_BUG << "Attempted to set IV on Google QUIC crypter";
    return false;
  }
  DCHECK_EQ(iv.size(), nonce_size_);
  if (iv.size() != nonce_size_) {
    return false;
  }
  memcpy(iv_, iv.data(), iv.size());
  return true;
}

bool AeadBaseEncrypter::Encrypt(quiche::QuicheStringPiece nonce,
                                quiche::QuicheStringPiece associated_data,
                                quiche::QuicheStringPiece plaintext,
                                unsigned char* output) {
  DCHECK_EQ(nonce.size(), nonce_size_);

  size_t ciphertext_len;
  if (!EVP_AEAD_CTX_seal(
          ctx_.get(), output, &ciphertext_len,
          plaintext.size() + auth_tag_size_,
          reinterpret_cast<const uint8_t*>(nonce.data()), nonce.size(),
          reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(),
          reinterpret_cast<const uint8_t*>(associated_data.data()),
          associated_data.size())) {
    DLogOpenSslErrors();
    return false;
  }

  return true;
}

bool AeadBaseEncrypter::EncryptPacket(uint64_t packet_number,
                                      quiche::QuicheStringPiece associated_data,
                                      quiche::QuicheStringPiece plaintext,
                                      char* output,
                                      size_t* output_length,
                                      size_t max_output_length) {
  size_t ciphertext_size = GetCiphertextSize(plaintext.length());
  if (max_output_length < ciphertext_size) {
    return false;
  }
  // TODO(ianswett): Introduce a check to ensure that we don't encrypt with the
  // same packet number twice.
  QUIC_ALIGNED(4) char nonce_buffer[kMaxNonceSize];
  memcpy(nonce_buffer, iv_, nonce_size_);
  size_t prefix_len = nonce_size_ - sizeof(packet_number);
  if (use_ietf_nonce_construction_) {
    for (size_t i = 0; i < sizeof(packet_number); ++i) {
      nonce_buffer[prefix_len + i] ^=
          (packet_number >> ((sizeof(packet_number) - i - 1) * 8)) & 0xff;
    }
  } else {
    memcpy(nonce_buffer + prefix_len, &packet_number, sizeof(packet_number));
  }

  if (!Encrypt(quiche::QuicheStringPiece(nonce_buffer, nonce_size_),
               associated_data, plaintext,
               reinterpret_cast<unsigned char*>(output))) {
    return false;
  }
  *output_length = ciphertext_size;
  return true;
}

size_t AeadBaseEncrypter::GetKeySize() const {
  return key_size_;
}

size_t AeadBaseEncrypter::GetNoncePrefixSize() const {
  return nonce_size_ - sizeof(QuicPacketNumber);
}

size_t AeadBaseEncrypter::GetIVSize() const {
  return nonce_size_;
}

size_t AeadBaseEncrypter::GetMaxPlaintextSize(size_t ciphertext_size) const {
  return ciphertext_size - std::min(ciphertext_size, auth_tag_size_);
}

size_t AeadBaseEncrypter::GetCiphertextSize(size_t plaintext_size) const {
  return plaintext_size + auth_tag_size_;
}

quiche::QuicheStringPiece AeadBaseEncrypter::GetKey() const {
  return quiche::QuicheStringPiece(reinterpret_cast<const char*>(key_),
                                   key_size_);
}

quiche::QuicheStringPiece AeadBaseEncrypter::GetNoncePrefix() const {
  return quiche::QuicheStringPiece(reinterpret_cast<const char*>(iv_),
                                   GetNoncePrefixSize());
}

}  // namespace quic
