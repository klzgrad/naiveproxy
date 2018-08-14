// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_CRYPTO_CURVE25519_KEY_EXCHANGE_H_
#define NET_THIRD_PARTY_QUIC_CORE_CRYPTO_CURVE25519_KEY_EXCHANGE_H_

#include <cstdint>

#include "base/compiler_specific.h"
#include "net/third_party/quic/core/crypto/key_exchange.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

class QuicRandom;

// Curve25519KeyExchange implements a KeyExchange using elliptic-curve
// Diffie-Hellman on curve25519. See http://cr.yp.to/ecdh.html
class QUIC_EXPORT_PRIVATE Curve25519KeyExchange : public KeyExchange {
 public:
  ~Curve25519KeyExchange() override;

  // New creates a new object from a private key. If the private key is
  // invalid, nullptr is returned.
  static std::unique_ptr<Curve25519KeyExchange> New(
      QuicStringPiece private_key);

  // NewPrivateKey returns a private key, generated from |rand|, suitable for
  // passing to |New|.
  static QuicString NewPrivateKey(QuicRandom* rand);

  // KeyExchange interface.
  const Factory& GetFactory() const override;
  bool CalculateSharedKey(QuicStringPiece peer_public_value,
                          QuicString* shared_key) const override;
  QuicStringPiece public_value() const override;

 private:
  Curve25519KeyExchange();

  uint8_t private_key_[32];
  uint8_t public_key_[32];
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_CRYPTO_CURVE25519_KEY_EXCHANGE_H_
