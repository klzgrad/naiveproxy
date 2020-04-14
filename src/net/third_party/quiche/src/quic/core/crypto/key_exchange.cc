// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/curve25519_key_exchange.h"
#include "net/third_party/quiche/src/quic/core/crypto/p256_key_exchange.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

std::unique_ptr<SynchronousKeyExchange> CreateLocalSynchronousKeyExchange(
    QuicTag type,
    quiche::QuicheStringPiece private_key) {
  switch (type) {
    case kC255:
      return Curve25519KeyExchange::New(private_key);
    case kP256:
      return P256KeyExchange::New(private_key);
    default:
      QUIC_BUG << "Unknown key exchange method: " << QuicTagToString(type);
      return nullptr;
  }
}

std::unique_ptr<SynchronousKeyExchange> CreateLocalSynchronousKeyExchange(
    QuicTag type,
    QuicRandom* rand) {
  switch (type) {
    case kC255:
      return Curve25519KeyExchange::New(rand);
      break;
    case kP256:
      return P256KeyExchange::New();
      break;
    default:
      QUIC_BUG << "Unknown key exchange method: " << QuicTagToString(type);
      return nullptr;
  }
}

}  // namespace quic
