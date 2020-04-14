// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_FRAMES_QUIC_MESSAGE_FRAME_H_
#define QUICHE_QUIC_CORE_FRAMES_QUIC_MESSAGE_FRAME_H_

#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_span.h"

namespace quic {

typedef QuicInlinedVector<QuicMemSlice, 1> QuicMessageData;

struct QUIC_EXPORT_PRIVATE QuicMessageFrame {
  QuicMessageFrame();
  explicit QuicMessageFrame(QuicMessageId message_id);
  QuicMessageFrame(QuicMessageId message_id, QuicMemSliceSpan span);
  QuicMessageFrame(const char* data, QuicPacketLength length);

  QuicMessageFrame(const QuicMessageFrame& other) = delete;
  QuicMessageFrame& operator=(const QuicMessageFrame& other) = delete;

  QuicMessageFrame(QuicMessageFrame&& other) = default;
  QuicMessageFrame& operator=(QuicMessageFrame&& other) = default;

  ~QuicMessageFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicMessageFrame& s);

  // message_id is only used on the sender side and does not get serialized on
  // wire.
  QuicMessageId message_id;
  // Not owned, only used on read path.
  const char* data;
  // Total length of message_data, must be fit into one packet.
  QuicPacketLength message_length;

  // The actual message data which is reference counted, used on write path.
  QuicMessageData message_data;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_FRAMES_QUIC_MESSAGE_FRAME_H_
