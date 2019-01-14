// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_MESSAGE_FRAME_H_
#define NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_MESSAGE_FRAME_H_

#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_string_piece.h"

namespace quic {

struct QUIC_EXPORT_PRIVATE QuicMessageFrame {
  QuicMessageFrame();
  QuicMessageFrame(QuicMessageId message_id, QuicStringPiece message_data);
  ~QuicMessageFrame();

  friend QUIC_EXPORT_PRIVATE std::ostream& operator<<(
      std::ostream& os,
      const QuicMessageFrame& s);

  // message_id is only used on the sender side and does not get serialized on
  // wire.
  QuicMessageId message_id;
  // The actual data is not owned.
  QuicStringPiece message_data;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_FRAMES_QUIC_MESSAGE_FRAME_H_
