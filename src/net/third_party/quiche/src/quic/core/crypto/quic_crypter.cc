// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/crypto/quic_crypter.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

bool QuicCrypter::SetNoncePrefixOrIV(
    const ParsedQuicVersion& version,
    quiche::QuicheStringPiece nonce_prefix_or_iv) {
  if (version.UsesInitialObfuscators()) {
    return SetIV(nonce_prefix_or_iv);
  }
  return SetNoncePrefix(nonce_prefix_or_iv);
}

}  // namespace quic
