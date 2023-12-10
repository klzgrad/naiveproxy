// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_SIMPLE_TICKET_CRYPTER_H_
#define QUICHE_QUIC_TOOLS_SIMPLE_TICKET_CRYPTER_H_

#include "openssl/aead.h"
#include "quiche/quic/core/crypto/proof_source.h"
#include "quiche/quic/core/quic_clock.h"
#include "quiche/quic/core/quic_time.h"

namespace quic {

// SimpleTicketCrypter implements the QUIC ProofSource::TicketCrypter interface.
// It generates a random key at startup and every 7 days it rotates the key,
// keeping track of the previous key used to facilitate decrypting older
// tickets. This implementation is not suitable for server setups where multiple
// servers need to share keys.
class QUIC_NO_EXPORT SimpleTicketCrypter
    : public quic::ProofSource::TicketCrypter {
 public:
  explicit SimpleTicketCrypter(QuicClock* clock);
  ~SimpleTicketCrypter() override;

  size_t MaxOverhead() override;
  std::vector<uint8_t> Encrypt(absl::string_view in,
                               absl::string_view encryption_key) override;
  void Decrypt(
      absl::string_view in,
      std::shared_ptr<quic::ProofSource::DecryptCallback> callback) override;

 private:
  std::vector<uint8_t> Decrypt(absl::string_view in);

  void MaybeRotateKeys();

  static constexpr size_t kKeySize = 16;

  struct Key {
    uint8_t key[kKeySize];
    bssl::ScopedEVP_AEAD_CTX aead_ctx;
    QuicTime expiration = QuicTime::Zero();
  };

  std::unique_ptr<Key> NewKey();

  std::unique_ptr<Key> current_key_;
  std::unique_ptr<Key> previous_key_;
  uint8_t key_epoch_ = 0;
  QuicClock* clock_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_SIMPLE_TICKET_CRYPTER_H_
