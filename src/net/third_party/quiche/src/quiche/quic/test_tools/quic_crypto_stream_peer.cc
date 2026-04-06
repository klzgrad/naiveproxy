// Copyright (c) 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_crypto_stream_peer.h"

#include <cstdint>

#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"

namespace quic::test {

uint64_t QuicCryptoStreamPeer::BytesReadOnLevel(const QuicCryptoStream& stream,
                                                EncryptionLevel level) {
  return stream.substreams_[QuicUtils::GetPacketNumberSpace(level)]
      .sequencer.NumBytesConsumed();
}

uint64_t QuicCryptoStreamPeer::BytesSentOnLevel(const QuicCryptoStream& stream,
                                                EncryptionLevel level) {
  return stream.substreams_[QuicUtils::GetPacketNumberSpace(level)]
      .send_buffer->stream_bytes_written();
}

}  // namespace quic::test
