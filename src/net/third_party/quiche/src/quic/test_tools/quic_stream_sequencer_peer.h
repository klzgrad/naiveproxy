// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_PEER_H_

#include "net/third_party/quiche/src/quic/core/quic_packets.h"

namespace quic {

class QuicStreamSequencer;

namespace test {

class QuicStreamSequencerPeer {
 public:
  QuicStreamSequencerPeer() = delete;

  static size_t GetNumBufferedBytes(QuicStreamSequencer* sequencer);

  static QuicStreamOffset GetCloseOffset(QuicStreamSequencer* sequencer);

  static bool IsUnderlyingBufferAllocated(QuicStreamSequencer* sequencer);

  static void SetFrameBufferTotalBytesRead(QuicStreamSequencer* sequencer,
                                           QuicStreamOffset total_bytes_read);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_STREAM_SEQUENCER_PEER_H_
