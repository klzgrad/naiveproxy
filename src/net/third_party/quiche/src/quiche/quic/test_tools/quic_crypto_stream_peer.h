// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_CRYPTO_STREAM_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_CRYPTO_STREAM_PEER_H_

#include <cstdint>

#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_types.h"

namespace quic::test {

class QuicCryptoStreamPeer {
 public:
  QuicCryptoStreamPeer() = delete;

  static uint64_t BytesReadOnLevel(const QuicCryptoStream& stream,
                                   EncryptionLevel level);
  static uint64_t BytesSentOnLevel(const QuicCryptoStream& stream,
                                   EncryptionLevel level);
};

}  // namespace quic::test

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_CRYPTO_STREAM_PEER_H_
