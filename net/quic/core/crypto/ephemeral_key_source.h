// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_CRYPTO_EPHEMERAL_KEY_SOURCE_H_
#define NET_QUIC_CORE_CRYPTO_EPHEMERAL_KEY_SOURCE_H_

#include <string>

#include "net/quic/core/quic_time.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

class KeyExchange;
class QuicRandom;

// EphemeralKeySource manages and rotates ephemeral keys as they can be reused
// for several connections in a short space of time. Since the implementation
// of this may involve locking or thread-local data, this interface abstracts
// that away.
class QUIC_EXPORT_PRIVATE EphemeralKeySource {
 public:
  virtual ~EphemeralKeySource() {}

  // CalculateForwardSecureKey generates an ephemeral public/private key pair
  // using the algorithm |key_exchange|, sets |*public_value| to the public key
  // and returns the shared key between |peer_public_value| and the private
  // key. |*public_value| will be sent to the peer to be used with the peer's
  // private key.
  virtual std::string CalculateForwardSecureKey(
      const KeyExchange* key_exchange,
      QuicRandom* rand,
      QuicTime now,
      QuicStringPiece peer_public_value,
      std::string* public_value) = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_CRYPTO_EPHEMERAL_KEY_SOURCE_H_
