// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_PEER_H_

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_send_buffer.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_sequencer.h"

namespace quic {

class QuicStream;
class QuicSession;

namespace test {

class QuicStreamPeer {
 public:
  QuicStreamPeer() = delete;

  static void SetWriteSideClosed(bool value, QuicStream* stream);
  static void SetStreamBytesWritten(QuicStreamOffset stream_bytes_written,
                                    QuicStream* stream);
  static bool read_side_closed(QuicStream* stream);
  static void CloseReadSide(QuicStream* stream);

  static bool StreamContributesToConnectionFlowControl(QuicStream* stream);

  static QuicStreamSequencer* sequencer(QuicStream* stream);
  static QuicSession* session(QuicStream* stream);

  static QuicStreamSendBuffer& SendBuffer(QuicStream* stream);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_PEER_H_
