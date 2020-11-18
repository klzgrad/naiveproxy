// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_secret_boxer.h"

#include "third_party/boringssl/src/include/openssl/aead.h"
#include "third_party/boringssl/src/include/openssl/err.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// kSIVNonceSize contains the number of bytes of nonce in each AES-GCM-SIV box.
// AES-GCM-SIV takes a 12-byte nonce and, since the messages are so small, each
// key is good for more than 2^64 source-address tokens. See table 1 of
// https://eprint.iacr.org/2017/168.pdf
static const size_t kSIVNonceSize = 12;

// AES-GCM-SIV comes in AES-128 and AES-256 flavours. The AES-256 version is
// used here so that the key size matches the 256-bit XSalsa20 keys that we
// used to use.
static const size_t kBoxKeySize = 32;

struct CryptoSecretBoxer::State {
  // ctxs are the initialised AEAD contexts. These objects contain the
  // scheduled AES state for each of the keys.
  std::vector<bssl::UniquePtr<EVP_AEAD_CTX>> ctxs;
};

CryptoSecretBoxer::CryptoSecretBoxer() {}

CryptoSecretBoxer::~CryptoSecretBoxer() {}

// static
size_t CryptoSecretBoxer::GetKeySize() {
  return kBoxKeySize;
}

// kAEAD is the AEAD used for boxing: AES-256-GCM-SIV.
static const EVP_AEAD* (*const kAEAD)() = EVP_aead_aes_256_gcm_siv;

void CryptoSecretBoxer::SetKeys(const std::vector<std::string>& keys) {
  DCHECK(!keys.empty());
  const EVP_AEAD* const aead = kAEAD();
  std::unique_ptr<State> new_state(new State);

  for (const std::string& key : keys) {
    DCHECK_EQ(kBoxKeySize, key.size());
    bssl::UniquePtr<EVP_AEAD_CTX> ctx(
        EVP_AEAD_CTX_new(aead, reinterpret_cast<const uint8_t*>(key.data()),
                         key.size(), EVP_AEAD_DEFAULT_TAG_LENGTH));
    if (!ctx) {
      ERR_clear_error();
      QUIC_LOG(DFATAL) << "EVP_AEAD_CTX_init failed";
      return;
    }

    new_state->ctxs.push_back(std::move(ctx));
  }

  QuicWriterMutexLock l(&lock_);
  state_ = std::move(new_state);
}

std::string CryptoSecretBoxer::Box(QuicRandom* rand,
                                   quiche::QuicheStringPiece plaintext) const {
  // The box is formatted as:
  //   12 bytes of random nonce
  //   n bytes of ciphertext
  //   16 bytes of authenticator
  size_t out_len =
      kSIVNonceSize + plaintext.size() + EVP_AEAD_max_overhead(kAEAD());

  std::string ret;
  ret.resize(out_len);
  uint8_t* out = reinterpret_cast<uint8_t*>(const_cast<char*>(ret.data()));

  // Write kSIVNonceSize bytes of random nonce to the beginning of the output
  // buffer.
  rand->RandBytes(out, kSIVNonceSize);
  const uint8_t* const nonce = out;
  out += kSIVNonceSize;
  out_len -= kSIVNonceSize;

  size_t bytes_written;
  {
    QuicReaderMutexLock l(&lock_);
    if (!EVP_AEAD_CTX_seal(state_->ctxs[0].get(), out, &bytes_written, out_len,
                           nonce, kSIVNonceSize,
                           reinterpret_cast<const uint8_t*>(plaintext.data()),
                           plaintext.size(), nullptr, 0)) {
      ERR_clear_error();
      QUIC_LOG(DFATAL) << "EVP_AEAD_CTX_seal failed";
      return "";
    }
  }

  DCHECK_EQ(out_len, bytes_written);
  return ret;
}

bool CryptoSecretBoxer::Unbox(quiche::QuicheStringPiece in_ciphertext,
                              std::string* out_storage,
                              quiche::QuicheStringPiece* out) const {
  if (in_ciphertext.size() < kSIVNonceSize) {
    return false;
  }

  const uint8_t* const nonce =
      reinterpret_cast<const uint8_t*>(in_ciphertext.data());
  const uint8_t* const ciphertext = nonce + kSIVNonceSize;
  const size_t ciphertext_len = in_ciphertext.size() - kSIVNonceSize;

  out_storage->resize(ciphertext_len);

  bool ok = false;
  {
    QuicReaderMutexLock l(&lock_);
    for (const bssl::UniquePtr<EVP_AEAD_CTX>& ctx : state_->ctxs) {
      size_t bytes_written;
      if (EVP_AEAD_CTX_open(ctx.get(),
                            reinterpret_cast<uint8_t*>(
                                const_cast<char*>(out_storage->data())),
                            &bytes_written, ciphertext_len, nonce,
                            kSIVNonceSize, ciphertext, ciphertext_len, nullptr,
                            0)) {
        ok = true;
        *out = quiche::QuicheStringPiece(out_storage->data(), bytes_written);
        break;
      }

      ERR_clear_error();
    }
  }

  return ok;
}

}  // namespace quic
