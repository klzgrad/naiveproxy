// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_stream_peer.h"

#include <list>

#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/test_tools/quic_flow_controller_peer.h"
#include "quiche/quic/test_tools/quic_stream_send_buffer_peer.h"

namespace quic {
namespace test {

// static
void QuicStreamPeer::SetWriteSideClosed(bool value, QuicStream* stream) {
  stream->write_side_closed_ = value;
}

// static
void QuicStreamPeer::SetStreamBytesWritten(
    QuicStreamOffset stream_bytes_written, QuicStream* stream) {
  stream->send_buffer_.stream_bytes_written_ = stream_bytes_written;
  stream->send_buffer_.stream_bytes_outstanding_ = stream_bytes_written;
  QuicStreamSendBufferPeer::SetStreamOffset(&stream->send_buffer_,
                                            stream_bytes_written);
}

// static
void QuicStreamPeer::SetSendWindowOffset(QuicStream* stream,
                                         QuicStreamOffset offset) {
  QuicFlowControllerPeer::SetSendWindowOffset(&*stream->flow_controller_,
                                              offset);
}

// static
QuicByteCount QuicStreamPeer::bytes_consumed(QuicStream* stream) {
  return stream->flow_controller_->bytes_consumed();
}

// static
void QuicStreamPeer::SetReceiveWindowOffset(QuicStream* stream,
                                            QuicStreamOffset offset) {
  QuicFlowControllerPeer::SetReceiveWindowOffset(&*stream->flow_controller_,
                                                 offset);
}

// static
void QuicStreamPeer::SetMaxReceiveWindow(QuicStream* stream,
                                         QuicStreamOffset size) {
  QuicFlowControllerPeer::SetMaxReceiveWindow(&*stream->flow_controller_, size);
}

// static
QuicByteCount QuicStreamPeer::SendWindowSize(QuicStream* stream) {
  return stream->flow_controller_->SendWindowSize();
}

// static
QuicStreamOffset QuicStreamPeer::ReceiveWindowOffset(QuicStream* stream) {
  return QuicFlowControllerPeer::ReceiveWindowOffset(
      &*stream->flow_controller_);
}

// static
QuicByteCount QuicStreamPeer::ReceiveWindowSize(QuicStream* stream) {
  return QuicFlowControllerPeer::ReceiveWindowSize(&*stream->flow_controller_);
}

// static
QuicStreamOffset QuicStreamPeer::SendWindowOffset(QuicStream* stream) {
  return stream->flow_controller_->send_window_offset();
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

// static
void QuicStreamPeer::SetFinReceived(QuicStream* stream) {
  stream->fin_received_ = true;
}

// static
void QuicStreamPeer::SetFinSent(QuicStream* stream) {
  stream->fin_sent_ = true;
}

}  // namespace test
}  // namespace quic
