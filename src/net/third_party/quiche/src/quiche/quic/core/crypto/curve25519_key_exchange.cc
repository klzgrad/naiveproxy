// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/curve25519_key_exchange.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "openssl/curve25519.h"
#include "quiche/quic/core/crypto/quic_random.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

Curve25519KeyExchange::Curve25519KeyExchange() {}

Curve25519KeyExchange::~Curve25519KeyExchange() {}

// static
std::unique_ptr<Curve25519KeyExchange> Curve25519KeyExchange::New(
    QuicRandom* rand) {
  std::unique_ptr<Curve25519KeyExchange> result =
      New(Curve25519KeyExchange::NewPrivateKey(rand));
  QUIC_BUG_IF(quic_bug_12891_1, result == nullptr);
  return result;
}

// static
std::unique_ptr<Curve25519KeyExchange> Curve25519KeyExchange::New(
    absl::string_view private_key) {
  // We don't want to #include the BoringSSL headers in the public header file,
  // so we use literals for the sizes of private_key_ and public_key_. Here we
  // assert that those values are equal to the values from the BoringSSL
  // header.
  static_assert(
      sizeof(Curve25519KeyExchange::private_key_) == X25519_PRIVATE_KEY_LEN,
      "header out of sync");
  static_assert(
      sizeof(Curve25519KeyExchange::public_key_) == X25519_PUBLIC_VALUE_LEN,
      "header out of sync");

  if (private_key.size() != X25519_PRIVATE_KEY_LEN) {
    return nullptr;
  }

  // Use absl::WrapUnique(new) instead of std::make_unique because
  // Curve25519KeyExchange has a private constructor.
  auto ka = absl::WrapUnique(new Curve25519KeyExchange);
  memcpy(ka->private_key_, private_key.data(), X25519_PRIVATE_KEY_LEN);
  X25519_public_from_private(ka->public_key_, ka->private_key_);
  return ka;
}

// static
std::string Curve25519KeyExchange::NewPrivateKey(QuicRandom* rand) {
  uint8_t private_key[X25519_PRIVATE_KEY_LEN];
  rand->RandBytes(private_key, sizeof(private_key));
  return std::string(reinterpret_cast<char*>(private_key), sizeof(private_key));
}

bool Curve25519KeyExchange::CalculateSharedKeySync(
    absl::string_view peer_public_value, std::string* shared_key) const {
  if (peer_public_value.size() != X25519_PUBLIC_VALUE_LEN) {
    return false;
  }

  uint8_t result[X25519_PUBLIC_VALUE_LEN];
  if (!X25519(result, private_key_,
              reinterpret_cast<const uint8_t*>(peer_public_value.data()))) {
    return false;
  }

  shared_key->assign(reinterpret_cast<char*>(result), sizeof(result));
  return true;
}

absl::string_view Curve25519KeyExchange::public_value() const {
  return absl::string_view(reinterpret_cast<const char*>(public_key_),
                           sizeof(public_key_));
}

}  // namespace quic
