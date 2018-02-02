// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_STREAM_SEND_BUFFER_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_STREAM_SEND_BUFFER_PEER_H_

#include "net/quic/core/quic_stream_send_buffer.h"

namespace net {

namespace test {

class QuicStreamSendBufferPeer {
 public:
  static void SetStreamOffset(QuicStreamSendBuffer* send_buffer,
                              QuicStreamOffset stream_offset);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_STREAM_SEND_BUFFER_PEER_H_
