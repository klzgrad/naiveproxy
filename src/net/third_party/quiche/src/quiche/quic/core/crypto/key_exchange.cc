// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/crypto/key_exchange.h"

#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/crypto/curve25519_key_exchange.h"
#include "quiche/quic/core/crypto/p256_key_exchange.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"

namespace quic {

std::unique_ptr<SynchronousKeyExchange> CreateLocalSynchronousKeyExchange(
    QuicTag type, absl::string_view private_key) {
  switch (type) {
    case kC255:
      return Curve25519KeyExchange::New(private_key);
    case kP256:
      return P256KeyExchange::New(private_key);
    default:
      QUIC_BUG(quic_bug_10712_1)
          << "Unknown key exchange method: " << QuicTagToString(type);
      return nullptr;
  }
}

std::unique_ptr<SynchronousKeyExchange> CreateLocalSynchronousKeyExchange(
    QuicTag type, QuicRandom* rand) {
  switch (type) {
    case kC255:
      return Curve25519KeyExchange::New(rand);
    case kP256:
      return P256KeyExchange::New();
    default:
      QUIC_BUG(quic_bug_10712_2)
          << "Unknown key exchange method: " << QuicTagToString(type);
      return nullptr;
  }
}

}  // namespace quic
