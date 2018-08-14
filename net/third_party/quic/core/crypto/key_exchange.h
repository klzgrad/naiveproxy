// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_CRYPTO_KEY_EXCHANGE_H_
#define NET_THIRD_PARTY_QUIC_CORE_CRYPTO_KEY_EXCHANGE_H_

#include <memory>

#include "net/third_party/quic/core/crypto/crypto_protocol.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

class QuicRandom;

// KeyExchange is an abstract class that provides an interface to a
// key-exchange primitive.
class QUIC_EXPORT_PRIVATE KeyExchange {
 public:
  virtual ~KeyExchange() {}

  class Factory {
   public:
    virtual ~Factory() = default;
    Factory(const Factory&) = delete;
    Factory& operator=(const Factory&) = delete;

    // Generates a new public, private key pair. (This is intended for
    // servers that need to generate forward-secure keys.)
    virtual std::unique_ptr<KeyExchange> Create(QuicRandom* rand) const = 0;

    // Returns the tag value that identifies this key exchange function.
    virtual QuicTag tag() const = 0;

   protected:
    Factory() = default;
  };

  // Get a reference to the singleton Factory object for this KeyExchange type.
  virtual const Factory& GetFactory() const = 0;

  // CalculateSharedKey computes the shared key between the local private key
  // (which is implicitly known by a KeyExchange object) and a public value
  // from the peer.
  virtual bool CalculateSharedKey(QuicStringPiece peer_public_value,
                                  QuicString* shared_key) const = 0;

  // public_value returns the local public key which can be sent to a peer in
  // order to complete a key exchange. The returned QuicStringPiece is a
  // reference to a member of the KeyExchange and is only valid for as long as
  // the KeyExchange exists.
  virtual QuicStringPiece public_value() const = 0;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_CRYPTO_KEY_EXCHANGE_H_
