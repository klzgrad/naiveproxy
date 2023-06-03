// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/simple_ticket_crypter.h"

#include "openssl/aead.h"
#include "openssl/rand.h"

namespace quic {

namespace {

constexpr QuicTime::Delta kTicketKeyLifetime =
    QuicTime::Delta::FromSeconds(60 * 60 * 24 * 7);

// The format of an encrypted ticket is 1 byte for the key epoch, followed by
// 16 bytes of IV, followed by the output from the AES-GCM Seal operation. The
// seal operation has an overhead of 16 bytes for its auth tag.
constexpr size_t kEpochSize = 1;
constexpr size_t kIVSize = 16;
constexpr size_t kAuthTagSize = 16;

// Offsets into the ciphertext to make message parsing easier.
constexpr size_t kIVOffset = kEpochSize;
constexpr size_t kMessageOffset = kIVOffset + kIVSize;

}  // namespace

SimpleTicketCrypter::SimpleTicketCrypter(QuicClock* clock) : clock_(clock) {
  RAND_bytes(&key_epoch_, 1);
  current_key_ = NewKey();
}

SimpleTicketCrypter::~SimpleTicketCrypter() = default;

size_t SimpleTicketCrypter::MaxOverhead() {
  return kEpochSize + kIVSize + kAuthTagSize;
}

std::vector<uint8_t> SimpleTicketCrypter::Encrypt(
    absl::string_view in, absl::string_view encryption_key) {
  // This class is only used in Chromium, in which the |encryption_key| argument
  // will never be populated and an internally-cached key should be used for
  // encrypting tickets.
  QUICHE_DCHECK(encryption_key.empty());
  MaybeRotateKeys();
  std::vector<uint8_t> out(in.size() + MaxOverhead());
  out[0] = key_epoch_;
  RAND_bytes(out.data() + kIVOffset, kIVSize);
  size_t out_len;
  const EVP_AEAD_CTX* ctx = current_key_->aead_ctx.get();
  if (!EVP_AEAD_CTX_seal(ctx, out.data() + kMessageOffset, &out_len,
                         out.size() - kMessageOffset, out.data() + kIVOffset,
                         kIVSize, reinterpret_cast<const uint8_t*>(in.data()),
                         in.size(), nullptr, 0)) {
    return std::vector<uint8_t>();
  }
  out.resize(out_len + kMessageOffset);
  return out;
}

std::vector<uint8_t> SimpleTicketCrypter::Decrypt(absl::string_view in) {
  MaybeRotateKeys();
  if (in.size() < kMessageOffset) {
    return std::vector<uint8_t>();
  }
  const uint8_t* input = reinterpret_cast<const uint8_t*>(in.data());
  std::vector<uint8_t> out(in.size() - kMessageOffset);
  size_t out_len;
  const EVP_AEAD_CTX* ctx = current_key_->aead_ctx.get();
  if (input[0] != key_epoch_) {
    if (input[0] == static_cast<uint8_t>(key_epoch_ - 1) && previous_key_) {
      ctx = previous_key_->aead_ctx.get();
    } else {
      return std::vector<uint8_t>();
    }
  }
  if (!EVP_AEAD_CTX_open(ctx, out.data(), &out_len, out.size(),
                         input + kIVOffset, kIVSize, input + kMessageOffset,
                         in.size() - kMessageOffset, nullptr, 0)) {
    return std::vector<uint8_t>();
  }
  out.resize(out_len);
  return out;
}

void SimpleTicketCrypter::Decrypt(
    absl::string_view in,
    std::shared_ptr<quic::ProofSource::DecryptCallback> callback) {
  callback->Run(Decrypt(in));
}

void SimpleTicketCrypter::MaybeRotateKeys() {
  QuicTime now = clock_->ApproximateNow();
  if (current_key_->expiration < now) {
    previous_key_ = std::move(current_key_);
    current_key_ = NewKey();
    key_epoch_++;
  }
}

std::unique_ptr<SimpleTicketCrypter::Key> SimpleTicketCrypter::NewKey() {
  auto key = std::make_unique<SimpleTicketCrypter::Key>();
  RAND_bytes(key->key, kKeySize);
  EVP_AEAD_CTX_init(key->aead_ctx.get(), EVP_aead_aes_128_gcm(), key->key,
                    kKeySize, EVP_AEAD_DEFAULT_TAG_LENGTH, nullptr);
  key->expiration = clock_->ApproximateNow() + kTicketKeyLifetime;
  return key;
}

}  // namespace quic
