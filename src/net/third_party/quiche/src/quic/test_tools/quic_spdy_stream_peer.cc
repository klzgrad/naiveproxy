// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/quic_spdy_stream_peer.h"

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

// static
void QuicSpdyStreamPeer::set_ack_listener(
    QuicSpdyStream* stream,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  stream->set_ack_listener(std::move(ack_listener));
}

// static
const QuicIntervalSet<QuicStreamOffset>&
QuicSpdyStreamPeer::unacked_frame_headers_offsets(QuicSpdyStream* stream) {
  return stream->unacked_frame_headers_offsets_;
}

}  // namespace test
}  // namespace quic
