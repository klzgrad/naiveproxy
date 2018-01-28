// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/quic_stream_sequencer_peer.h"

#include "net/quic/core/quic_stream_sequencer.h"
#include "net/quic/test_tools/quic_stream_sequencer_buffer_peer.h"

using std::string;

namespace net {
namespace test {

// static
size_t QuicStreamSequencerPeer::GetNumBufferedBytes(
    QuicStreamSequencer* sequencer) {
  return sequencer->buffered_frames_.BytesBuffered();
}

// static
QuicStreamOffset QuicStreamSequencerPeer::GetCloseOffset(
    QuicStreamSequencer* sequencer) {
  return sequencer->close_offset_;
}

// static
bool QuicStreamSequencerPeer::IsUnderlyingBufferAllocated(
    QuicStreamSequencer* sequencer) {
  QuicStreamSequencerBufferPeer buffer_peer(&(sequencer->buffered_frames_));
  return buffer_peer.IsBufferAllocated();
}

}  // namespace test
}  // namespace net
