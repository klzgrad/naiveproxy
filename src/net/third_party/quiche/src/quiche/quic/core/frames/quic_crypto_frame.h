// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_CRYPTO_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_CRYPTO_FRAME_H_

#include <memory>
#include <ostream>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicCryptoFrame {
  QuicCryptoFrame() = default;
  QuicCryptoFrame(EncryptionLevel level, QuicStreamOffset offset,
                  QuicPacketLength data_length);
  QuicCryptoFrame(EncryptionLevel level, QuicStreamOffset offset,
                  absl::string_view data);
  ~QuicCryptoFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                                      const QuicCryptoFrame& s);

  // TODO(haoyuewang) Consider replace the EncryptionLevel here with
  // PacketNumberSpace.
  // When writing a crypto frame to a packet, the packet must be encrypted at
  // |level|. When a crypto frame is read, the encryption level of the packet it
  // was received in is put in |level|.
  EncryptionLevel level = ENCRYPTION_INITIAL;
  QuicPacketLength data_length = 0;
  // When reading, |data_buffer| points to the data that was received in the
  // frame. |data_buffer| is not used when writing.
  const char* data_buffer = nullptr;
  QuicStreamOffset offset = 0;  // Location of this data in the stream.

  QuicCryptoFrame(EncryptionLevel level, QuicStreamOffset offset,
                  const char* data_buffer, QuicPacketLength data_length);
};
static_assert(sizeof(QuicCryptoFrame) <= 64,
              "Keep the QuicCryptoFrame size to a cacheline.");

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_CRYPTO_FRAME_H_
