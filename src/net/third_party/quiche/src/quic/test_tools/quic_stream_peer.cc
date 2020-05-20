// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_stream_peer.h"

#include <list>

#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_stream_send_buffer_peer.h"

namespace quic {
namespace test {

// static
void QuicStreamPeer::SetWriteSideClosed(bool value, QuicStream* stream) {
  stream->write_side_closed_ = value;
}

// static
void QuicStreamPeer::SetStreamBytesWritten(
    QuicStreamOffset stream_bytes_written,
    QuicStream* stream) {
  stream->send_buffer_.stream_bytes_written_ = stream_bytes_written;
  stream->send_buffer_.stream_bytes_outstanding_ = stream_bytes_written;
  QuicStreamSendBufferPeer::SetStreamOffset(&stream->send_buffer_,
                                            stream_bytes_written);
}

// static
bool QuicStreamPeer::read_side_closed(QuicStream* stream) {
  return stream->read_side_closed_;
}

// static
void QuicStreamPeer::CloseReadSide(QuicStream* stream) {
  stream->CloseReadSide();
}

// static
bool QuicStreamPeer::StreamContributesToConnectionFlowControl(
    QuicStream* stream) {
  return stream->stream_contributes_to_connection_flow_control_;
}

// static
QuicStreamSequencer* QuicStreamPeer::sequencer(QuicStream* stream) {
  return &(stream->sequencer_);
}

// static
QuicSession* QuicStreamPeer::session(QuicStream* stream) {
  return stream->session();
}

// static
QuicStreamSendBuffer& QuicStreamPeer::SendBuffer(QuicStream* stream) {
  return stream->send_buffer_;
}

}  // namespace test
}  // namespace quic
