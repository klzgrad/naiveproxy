// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEND_BUFFER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEND_BUFFER_PEER_H_

#include "net/third_party/quiche/src/quic/core/quic_stream_send_buffer.h"

namespace quic {

namespace test {

class QuicStreamSendBufferPeer {
 public:
  static void SetStreamOffset(QuicStreamSendBuffer* send_buffer,
                              QuicStreamOffset stream_offset);

  static const BufferedSlice* CurrentWriteSlice(
      QuicStreamSendBuffer* send_buffer);

  static QuicStreamOffset EndOffset(QuicStreamSendBuffer* send_buffer);

  static QuicByteCount TotalLength(QuicStreamSendBuffer* send_buffer);

  static int32_t write_index(QuicStreamSendBuffer* send_buffer);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEND_BUFFER_PEER_H_
