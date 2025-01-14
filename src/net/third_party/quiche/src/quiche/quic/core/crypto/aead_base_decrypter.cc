// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/aead_base_decrypter.h"

#include <cstdint>
#include <string>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "openssl/crypto.h"
#include "openssl/err.h"
#include "openssl/evp.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/quiche_crypto_logging.h"

namespace quic {
using ::quiche::ClearOpenSslErrors;
using ::quiche::DLogOpenSslErrors;
namespace {

const EVP_AEAD* InitAndCall(const EVP_AEAD* (*aead_getter)()) {
  // Ensure BoringSSL is initialized before calling |aead_getter|. In Chromium,
  // the static initializer is disabled.
  CRYPTO_library_init();
  return aead_getter();
}

}  // namespace

AeadBaseDecrypter::AeadBaseDecrypter(const EVP_AEAD* (*aead_getter)(),
                                     size_t key_size, size_t auth_tag_size,
                                     size_t nonce_size,
                                     bool use_ietf_nonce_construction)
    : aead_alg_(InitAndCall(aead_getter)),
      key_size_(key_size),
      auth_tag_size_(auth_tag_size),
      nonce_size_(nonce_size),
      use_ietf_nonce_construction_(use_ietf_nonce_construction),
      have_preliminary_key_(false) {
  QUICHE_DCHECK_GT(256u, key_size);
  QUICHE_DCHECK_GT(256u, auth_tag_size);
  QUICHE_DCHECK_GT(256u, nonce_size);
  QUICHE_DCHECK_LE(key_size_, sizeof(key_));
  QUICHE_DCHECK_LE(nonce_size_, sizeof(iv_));
}

AeadBaseDecrypter::~AeadBaseDecrypter() {}

bool AeadBaseDecrypter::SetKey(absl::string_view key) {
  QUICHE_DCHECK_EQ(key.size(), key_size_);
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

bool AeadBaseDecrypter::SetNoncePrefix(absl::string_view nonce_prefix) {
  if (use_ietf_nonce_construction_) {
    QUIC_BUG(quic_bug_10709_1)
        << "Attempted to set nonce prefix on IETF QUIC crypter";
    return false;
  }
  QUICHE_DCHECK_EQ(nonce_prefix.size(), nonce_size_ - sizeof(QuicPacketNumber));
  if (nonce_prefix.size() != nonce_size_ - sizeof(QuicPacketNumber)) {
    return false;
  }
  memcpy(iv_, nonce_prefix.data(), nonce_prefix.size());
  return true;
}

bool AeadBaseDecrypter::SetIV(absl::string_view iv) {
  if (!use_ietf_nonce_construction_) {
    QUIC_BUG(quic_bug_10709_2) << "Attempted to set IV on Google QUIC crypter";
    return false;
  }
  QUICHE_DCHECK_EQ(iv.size(), nonce_size_);
  if (iv.size() != nonce_size_) {
    return false;
  }
  memcpy(iv_, iv.data(), iv.size());
  return true;
}

bool AeadBaseDecrypter::SetPreliminaryKey(absl::string_view key) {
  QUICHE_DCHECK(!have_preliminary_key_);
  SetKey(key);
  have_preliminary_key_ = true;

  return true;
}

bool AeadBaseDecrypter::SetDiversificationNonce(
    const DiversificationNonce& nonce) {
  if (!have_preliminary_key_) {
    return true;
  }

  std::string key, nonce_prefix;
  size_t prefix_size = nonce_size_;
  if (!use_ietf_nonce_construction_) {
    prefix_size -= sizeof(QuicPacketNumber);
  }
  DiversifyPreliminaryKey(
      absl::string_view(reinterpret_cast<const char*>(key_), key_size_),
      absl::string_view(reinterpret_cast<const char*>(iv_), prefix_size), nonce,
      key_size_, prefix_size, &key, &nonce_prefix);

  if (!SetKey(key) ||
      (!use_ietf_nonce_construction_ && !SetNoncePrefix(nonce_prefix)) ||
      (use_ietf_nonce_construction_ && !SetIV(nonce_prefix))) {
    QUICHE_DCHECK(false);
    return false;
  }

  have_preliminary_key_ = false;
  return true;
}

bool AeadBaseDecrypter::DecryptPacket(uint64_t packet_number,
                                      absl::string_view associated_data,
                                      absl::string_view ciphertext,
                                      char* output, size_t* output_length,
                                      size_t max_output_length) {
  if (ciphertext.length() < auth_tag_size_) {
    return false;
  }

  if (have_preliminary_key_) {
    QUIC_BUG(quic_bug_10709_3)
        << "Unable to decrypt while key diversification is pending";
    return false;
  }

  uint8_t nonce[kMaxNonceSize];
  memcpy(nonce, iv_, nonce_size_);
  size_t prefix_len = nonce_size_ - sizeof(packet_number);
  if (use_ietf_nonce_construction_) {
    for (size_t i = 0; i < sizeof(packet_number); ++i) {
      nonce[prefix_len + i] ^=
          (packet_number >> ((sizeof(packet_number) - i - 1) * 8)) & 0xff;
    }
  } else {
    memcpy(nonce + prefix_len, &packet_number, sizeof(packet_number));
  }
  if (!EVP_AEAD_CTX_open(
          ctx_.get(), reinterpret_cast<uint8_t*>(output), output_length,
          max_output_length, reinterpret_cast<const uint8_t*>(nonce),
          nonce_size_, reinterpret_cast<const uint8_t*>(ciphertext.data()),
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

size_t AeadBaseDecrypter::GetKeySize() const { return key_size_; }

size_t AeadBaseDecrypter::GetNoncePrefixSize() const {
  return nonce_size_ - sizeof(QuicPacketNumber);
}

size_t AeadBaseDecrypter::GetIVSize() const { return nonce_size_; }

absl::string_view AeadBaseDecrypter::GetKey() const {
  return absl::string_view(reinterpret_cast<const char*>(key_), key_size_);
}

absl::string_view AeadBaseDecrypter::GetNoncePrefix() const {
  return absl::string_view(reinterpret_cast<const char*>(iv_),
                           nonce_size_ - sizeof(QuicPacketNumber));
}

}  // namespace quic
