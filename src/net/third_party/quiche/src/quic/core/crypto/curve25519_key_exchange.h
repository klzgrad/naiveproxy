// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CRYPTO_CURVE25519_KEY_EXCHANGE_H_
#define QUICHE_QUIC_CORE_CRYPTO_CURVE25519_KEY_EXCHANGE_H_

#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/key_exchange.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicRandom;

// Curve25519KeyExchange implements a SynchronousKeyExchange using
// elliptic-curve Diffie-Hellman on curve25519. See http://cr.yp.to/ecdh.html
class QUIC_EXPORT_PRIVATE Curve25519KeyExchange
    : public SynchronousKeyExchange {
 public:
  ~Curve25519KeyExchange() override;

  // New generates a private key and then creates new key-exchange object.
  static std::unique_ptr<Curve25519KeyExchange> New(QuicRandom* rand);

  // New creates a new key-exchange object from a private key. If |private_key|
  // is invalid, nullptr is returned.
  static std::unique_ptr<Curve25519KeyExchange> New(
      quiche::QuicheStringPiece private_key);

  // NewPrivateKey returns a private key, generated from |rand|, suitable for
  // passing to |New|.
  static std::string NewPrivateKey(QuicRandom* rand);

  // SynchronousKeyExchange interface.
  bool CalculateSharedKeySync(quiche::QuicheStringPiece peer_public_value,
                              std::string* shared_key) const override;
  quiche::QuicheStringPiece public_value() const override;
  QuicTag type() const override { return kC255; }

 private:
  Curve25519KeyExchange();

  uint8_t private_key_[32];
  uint8_t public_key_[32];
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CRYPTO_CURVE25519_KEY_EXCHANGE_H_
