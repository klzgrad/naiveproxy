// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/crypto/curve25519_key_exchange.h"

#include <cstdint>

#include "net/quic/core/crypto/quic_random.h"
#include "third_party/boringssl/src/include/openssl/curve25519.h"

using std::string;

namespace net {

Curve25519KeyExchange::Curve25519KeyExchange() {}

Curve25519KeyExchange::~Curve25519KeyExchange() {}

// static
Curve25519KeyExchange* Curve25519KeyExchange::New(QuicStringPiece private_key) {
  Curve25519KeyExchange* ka;
  // We don't want to #include the BoringSSL headers in the public header file,
  // so we use literals for the sizes of private_key_ and public_key_. Here we
  // assert that those values are equal to the values from the BoringSSL
  // header.
  static_assert(sizeof(ka->private_key_) == X25519_PRIVATE_KEY_LEN,
                "header out of sync");
  static_assert(sizeof(ka->public_key_) == X25519_PUBLIC_VALUE_LEN,
                "header out of sync");

  if (private_key.size() != X25519_PRIVATE_KEY_LEN) {
    return nullptr;
  }

  ka = new Curve25519KeyExchange();
  memcpy(ka->private_key_, private_key.data(), X25519_PRIVATE_KEY_LEN);
  X25519_public_from_private(ka->public_key_, ka->private_key_);
  return ka;
}

// static
string Curve25519KeyExchange::NewPrivateKey(QuicRandom* rand) {
  uint8_t private_key[X25519_PRIVATE_KEY_LEN];
  rand->RandBytes(private_key, sizeof(private_key));
  return string(reinterpret_cast<char*>(private_key), sizeof(private_key));
}

KeyExchange* Curve25519KeyExchange::NewKeyPair(QuicRandom* rand) const {
  const string private_value = NewPrivateKey(rand);
  return Curve25519KeyExchange::New(private_value);
}

bool Curve25519KeyExchange::CalculateSharedKey(
    QuicStringPiece peer_public_value,
    string* out_result) const {
  if (peer_public_value.size() != X25519_PUBLIC_VALUE_LEN) {
    return false;
  }

  uint8_t result[X25519_PUBLIC_VALUE_LEN];
  if (!X25519(result, private_key_,
              reinterpret_cast<const uint8_t*>(peer_public_value.data()))) {
    return false;
  }

  out_result->assign(reinterpret_cast<char*>(result), sizeof(result));
  return true;
}

QuicStringPiece Curve25519KeyExchange::public_value() const {
  return QuicStringPiece(reinterpret_cast<const char*>(public_key_),
                         sizeof(public_key_));
}

QuicTag Curve25519KeyExchange::tag() const {
  return kC255;
}

}  // namespace net
