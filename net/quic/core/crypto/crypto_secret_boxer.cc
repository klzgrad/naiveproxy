// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/crypto_secret_boxer.h"

#include <memory>

#include "base/logging.h"
#include "base/strings/string_util.h"
#include "net/quic/core/crypto/quic_decrypter.h"
#include "net/quic/core/crypto/quic_encrypter.h"
#include "net/quic/core/crypto/quic_random.h"
#include "third_party/boringssl/src/include/openssl/aead.h"

using std::string;

namespace net {

// Defined kKeySize for GetKeySize() and SetKey().
static const size_t kKeySize = 16;

// kBoxNonceSize contains the number of bytes of nonce that we use in each box.
static const size_t kBoxNonceSize = 12;

struct CryptoSecretBoxer::State {
  // ctxs are the initialised AEAD contexts. These objects contain the
  // scheduled AES state for each of the keys.
  std::vector<bssl::UniquePtr<EVP_AEAD_CTX>> ctxs;
};

CryptoSecretBoxer::CryptoSecretBoxer() {}

CryptoSecretBoxer::~CryptoSecretBoxer() {}

// static
size_t CryptoSecretBoxer::GetKeySize() {
  return kKeySize;
}

// kAEAD is the AEAD used for boxing: AES-128-GCM-SIV.
static const EVP_AEAD* (*const kAEAD)() = EVP_aead_aes_128_gcm_siv;

void CryptoSecretBoxer::SetKeys(const std::vector<string>& keys) {
  DCHECK(!keys.empty());
  const EVP_AEAD* const aead = kAEAD();
  std::unique_ptr<State> new_state(new State);

  for (const string& key : keys) {
    DCHECK_EQ(kKeySize, key.size());
    bssl::UniquePtr<EVP_AEAD_CTX> ctx(
        EVP_AEAD_CTX_new(aead, reinterpret_cast<const uint8_t*>(key.data()),
                         key.size(), EVP_AEAD_DEFAULT_TAG_LENGTH));
    if (!ctx) {
      LOG(DFATAL) << "EVP_AEAD_CTX_init failed";
      return;
    }

    new_state->ctxs.push_back(std::move(ctx));
  }

  QuicWriterMutexLock l(&lock_);
  state_ = std::move(new_state);
}

string CryptoSecretBoxer::Box(QuicRandom* rand,
                              QuicStringPiece plaintext) const {
  // The box is formatted as:
  //   12 bytes of random nonce
  //   n bytes of ciphertext
  //   16 bytes of authenticator
  size_t out_len =
      kBoxNonceSize + plaintext.size() + EVP_AEAD_max_overhead(kAEAD());

  string ret;
  uint8_t* out = reinterpret_cast<uint8_t*>(base::WriteInto(&ret, out_len + 1));

  // Write kBoxNonceSize bytes of random nonce to the beginning of the output
  // buffer.
  rand->RandBytes(out, kBoxNonceSize);
  const uint8_t* const nonce = out;
  out += kBoxNonceSize;
  out_len -= kBoxNonceSize;

  size_t bytes_written;
  {
    QuicReaderMutexLock l(&lock_);
    if (!EVP_AEAD_CTX_seal(state_->ctxs[0].get(), out, &bytes_written, out_len,
                           nonce, kBoxNonceSize,
                           reinterpret_cast<const uint8_t*>(plaintext.data()),
                           plaintext.size(), nullptr, 0)) {
      LOG(DFATAL) << "EVP_AEAD_CTX_seal failed";
      return "";
    }
  }

  DCHECK_EQ(out_len, bytes_written);

  return ret;
}

bool CryptoSecretBoxer::Unbox(QuicStringPiece in_ciphertext,
                              string* out_storage,
                              QuicStringPiece* out) const {
  if (in_ciphertext.size() <= kBoxNonceSize) {
    return false;
  }

  const uint8_t* const nonce =
      reinterpret_cast<const uint8_t*>(in_ciphertext.data());
  const uint8_t* const ciphertext = nonce + kBoxNonceSize;
  const size_t ciphertext_len = in_ciphertext.size() - kBoxNonceSize;

  uint8_t* out_data = reinterpret_cast<uint8_t*>(
      base::WriteInto(out_storage, ciphertext_len + 1));

  QuicReaderMutexLock l(&lock_);
  for (const bssl::UniquePtr<EVP_AEAD_CTX>& ctx : state_->ctxs) {
    size_t bytes_written;
    if (EVP_AEAD_CTX_open(ctx.get(), out_data, &bytes_written, ciphertext_len,
                          nonce, kBoxNonceSize, ciphertext, ciphertext_len,
                          nullptr, 0)) {
      *out = QuicStringPiece(out_storage->data(), bytes_written);
      return true;
    }
  }

  return false;
}

}  // namespace net
