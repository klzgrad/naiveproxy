// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_P256_KEY_EXCHANGE_H_
#define NET_QUIC_CORE_CRYPTO_P256_KEY_EXCHANGE_H_

#include <cstdint>
#include <string>

#include "base/macros.h"
#include "net/quic/core/crypto/key_exchange.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"
#include "third_party/boringssl/src/include/openssl/base.h"

namespace net {

// P256KeyExchange implements a KeyExchange using elliptic-curve
// Diffie-Hellman on NIST P-256.
class QUIC_EXPORT_PRIVATE P256KeyExchange : public KeyExchange {
 public:
  ~P256KeyExchange() override;

  // New creates a new key exchange object from a private key. If
  // |private_key| is invalid, nullptr is returned.
  static P256KeyExchange* New(QuicStringPiece private_key);

  // |NewPrivateKey| returns a private key, suitable for passing to |New|.
  // If |NewPrivateKey| can't generate a private key, it returns an empty
  // string.
  static std::string NewPrivateKey();

  // KeyExchange interface.
  KeyExchange* NewKeyPair(QuicRandom* rand) const override;
  bool CalculateSharedKey(QuicStringPiece peer_public_value,
                          std::string* shared_key) const override;
  QuicStringPiece public_value() const override;
  QuicTag tag() const override;

 private:
  enum {
    // A P-256 field element consists of 32 bytes.
    kP256FieldBytes = 32,
    // A P-256 point in uncompressed form consists of 0x04 (to denote
    // that the point is uncompressed) followed by two, 32-byte field
    // elements.
    kUncompressedP256PointBytes = 1 + 2 * kP256FieldBytes,
    // The first byte in an uncompressed P-256 point.
    kUncompressedECPointForm = 0x04,
  };

  // P256KeyExchange wraps |private_key|, and expects |public_key| consists of
  // |kUncompressedP256PointBytes| bytes.
  P256KeyExchange(bssl::UniquePtr<EC_KEY> private_key,
                  const uint8_t* public_key);

  bssl::UniquePtr<EC_KEY> private_key_;
  // The public key stored as an uncompressed P-256 point.
  uint8_t public_key_[kUncompressedP256PointBytes];

  DISALLOW_COPY_AND_ASSIGN(P256KeyExchange);
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_P256_KEY_EXCHANGE_H_
