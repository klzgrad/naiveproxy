// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_DATAGRAM_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_DATAGRAM_FRAME_H_

#include "absl/container/inlined_vector.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/quiche_mem_slice.h"

namespace quic {

using QuicDatagramData = absl::InlinedVector<quiche::QuicheMemSlice, 1>;

struct QUICHE_EXPORT QuicDatagramFrame {
  QuicDatagramFrame() = default;
  explicit QuicDatagramFrame(QuicDatagramId datagram_id);
  QuicDatagramFrame(QuicDatagramId datagram_id,
                    absl::Span<quiche::QuicheMemSlice> span);
  QuicDatagramFrame(QuicDatagramId datagram_id, quiche::QuicheMemSlice slice);
  QuicDatagramFrame(const char* data, QuicPacketLength length);

  QuicDatagramFrame(const QuicDatagramFrame& other) = delete;
  QuicDatagramFrame& operator=(const QuicDatagramFrame& other) = delete;

  QuicDatagramFrame(QuicDatagramFrame&& other) = default;
  QuicDatagramFrame& operator=(QuicDatagramFrame&& other) = default;

  ~QuicDatagramFrame();

  friend QUICHE_EXPORT std::ostream& operator<<(std::ostream& os,
                                                const QuicDatagramFrame& s);

  // datagram_id is only used on the sender side and does not get serialized on
  // wire.
  QuicDatagramId datagram_id = 0;
  // Not owned, only used on read path.
  const char* data = nullptr;
  // Total length of datagram_data, must be fit into one packet.
  QuicPacketLength datagram_length = 0;

  // The actual datagram data which is reference counted, used on write path.
  QuicDatagramData datagram_data;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_DATAGRAM_FRAME_H_
