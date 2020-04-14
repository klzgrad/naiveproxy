// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/frames/quic_crypto_frame.h"

#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

QuicCryptoFrame::QuicCryptoFrame()
    : QuicCryptoFrame(ENCRYPTION_INITIAL, 0, nullptr, 0) {}

QuicCryptoFrame::QuicCryptoFrame(EncryptionLevel level,
                                 QuicStreamOffset offset,
                                 QuicPacketLength data_length)
    : QuicCryptoFrame(level, offset, nullptr, data_length) {}

QuicCryptoFrame::QuicCryptoFrame(EncryptionLevel level,
                                 QuicStreamOffset offset,
                                 quiche::QuicheStringPiece data)
    : QuicCryptoFrame(level, offset, data.data(), data.length()) {}

QuicCryptoFrame::QuicCryptoFrame(EncryptionLevel level,
                                 QuicStreamOffset offset,
                                 const char* data_buffer,
                                 QuicPacketLength data_length)
    : level(level),
      data_length(data_length),
      data_buffer(data_buffer),
      offset(offset) {}

QuicCryptoFrame::~QuicCryptoFrame() {}

std::ostream& operator<<(std::ostream& os,
                         const QuicCryptoFrame& stream_frame) {
  os << "{ level: " << static_cast<int>(stream_frame.level)
     << ", offset: " << stream_frame.offset
     << ", length: " << stream_frame.data_length << " }\n";
  return os;
}

}  // namespace quic
