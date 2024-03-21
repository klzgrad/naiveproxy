// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_AEAD_BASE_ENCRYPTER_H_
#define QUICHE_QUIC_CORE_CRYPTO_AEAD_BASE_ENCRYPTER_H_

#include <cstddef>

#include "absl/strings/string_view.h"
#include "openssl/aead.h"
#include "quiche/quic/core/crypto/quic_encrypter.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// AeadBaseEncrypter is the base class of AEAD QuicEncrypter subclasses.
class QUICHE_EXPORT AeadBaseEncrypter : public QuicEncrypter {
 public:
  // This takes the function pointer rather than the EVP_AEAD itself so
  // subclasses do not need to call CRYPTO_library_init.
  AeadBaseEncrypter(const EVP_AEAD* (*aead_getter)(), size_t key_size,
                    size_t auth_tag_size, size_t nonce_size,
                    bool use_ietf_nonce_construction);
  AeadBaseEncrypter(const AeadBaseEncrypter&) = delete;
  AeadBaseEncrypter& operator=(const AeadBaseEncrypter&) = delete;
  ~AeadBaseEncrypter() override;

  // QuicEncrypter implementation
  bool SetKey(absl::string_view key) override;
  bool SetNoncePrefix(absl::string_view nonce_prefix) override;
  bool SetIV(absl::string_view iv) override;
  bool EncryptPacket(uint64_t packet_number, absl::string_view associated_data,
                     absl::string_view plaintext, char* output,
                     size_t* output_length, size_t max_output_length) override;
  size_t GetKeySize() const override;
  size_t GetNoncePrefixSize() const override;
  size_t GetIVSize() const override;
  size_t GetMaxPlaintextSize(size_t ciphertext_size) const override;
  size_t GetCiphertextSize(size_t plaintext_size) const override;
  absl::string_view GetKey() const override;
  absl::string_view GetNoncePrefix() const override;

  // Necessary so unit tests can explicitly specify a nonce, instead of an IV
  // (or nonce prefix) and packet number.
  bool Encrypt(absl::string_view nonce, absl::string_view associated_data,
               absl::string_view plaintext, unsigned char* output);

 protected:
  // Make these constants available to the subclasses so that the subclasses
  // can assert at compile time their key_size_ and nonce_size_ do not
  // exceed the maximum.
  static const size_t kMaxKeySize = 32;
  enum : size_t { kMaxNonceSize = 12 };

 private:
  const EVP_AEAD* const aead_alg_;
  const size_t key_size_;
  const size_t auth_tag_size_;
  const size_t nonce_size_;
  const bool use_ietf_nonce_construction_;

  // The key.
  unsigned char key_[kMaxKeySize];
  // The IV used to construct the nonce.
  unsigned char iv_[kMaxNonceSize];

  bssl::ScopedEVP_AEAD_CTX ctx_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_AEAD_BASE_ENCRYPTER_H_
