// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_stream_send_buffer_peer.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_interval_deque_peer.h"

namespace quic {

namespace test {

// static
void QuicStreamSendBufferPeer::SetStreamOffset(
    QuicStreamSendBuffer* send_buffer,
    QuicStreamOffset stream_offset) {
  send_buffer->stream_offset_ = stream_offset;
}

// static
const BufferedSlice* QuicStreamSendBufferPeer::CurrentWriteSlice(
    QuicStreamSendBuffer* send_buffer) {
  auto wi = write_index(send_buffer);

  if (wi == -1) {
    return nullptr;
  }
  return QuicIntervalDequePeer::GetItem(&send_buffer->interval_deque_, wi);
}

QuicStreamOffset QuicStreamSendBufferPeer::EndOffset(
    QuicStreamSendBuffer* send_buffer) {
  return send_buffer->current_end_offset_;
}

// static
QuicByteCount QuicStreamSendBufferPeer::TotalLength(
    QuicStreamSendBuffer* send_buffer) {
  QuicByteCount length = 0;
  for (auto slice = send_buffer->interval_deque_.DataBegin();
       slice != send_buffer->interval_deque_.DataEnd(); ++slice) {
    length += slice->slice.length();
  }
  return length;
}

// static
int32_t QuicStreamSendBufferPeer::write_index(
    QuicStreamSendBuffer* send_buffer) {
  return QuicIntervalDequePeer::GetCachedIndex(&send_buffer->interval_deque_);
}

}  // namespace test

}  // namespace quic
