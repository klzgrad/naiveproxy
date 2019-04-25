// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/test_tools/quic_spdy_session_peer.h"

#include "net/third_party/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quic/core/quic_utils.h"

namespace quic {
namespace test {

// static
QuicHeadersStream* QuicSpdySessionPeer::GetHeadersStream(
    QuicSpdySession* session) {
  return session->headers_stream_.get();
}

// static
void QuicSpdySessionPeer::SetHeadersStream(QuicSpdySession* session,
                                           QuicHeadersStream* headers_stream) {
  session->headers_stream_.reset(headers_stream);
  if (headers_stream != nullptr) {
    session->RegisterStaticStream(headers_stream->id(), headers_stream);
  }
}

// static
const spdy::SpdyFramer& QuicSpdySessionPeer::GetSpdyFramer(
    QuicSpdySession* session) {
  return session->spdy_framer_;
}

void QuicSpdySessionPeer::SetHpackEncoderDebugVisitor(
    QuicSpdySession* session,
    std::unique_ptr<QuicHpackDebugVisitor> visitor) {
  session->SetHpackEncoderDebugVisitor(std::move(visitor));
}

void QuicSpdySessionPeer::SetHpackDecoderDebugVisitor(
    QuicSpdySession* session,
    std::unique_ptr<QuicHpackDebugVisitor> visitor) {
  session->SetHpackDecoderDebugVisitor(std::move(visitor));
}

void QuicSpdySessionPeer::SetMaxUncompressedHeaderBytes(
    QuicSpdySession* session,
    size_t set_max_uncompressed_header_bytes) {
  session->set_max_uncompressed_header_bytes(set_max_uncompressed_header_bytes);
}

// static
size_t QuicSpdySessionPeer::WriteHeadersOnHeadersStream(
    QuicSpdySession* session,
    QuicStreamId id,
    spdy::SpdyHeaderBlock headers,
    bool fin,
    spdy::SpdyPriority priority,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  return session->WriteHeadersOnHeadersStream(
      id, std::move(headers), fin, priority, std::move(ack_listener));
}

}  // namespace test
}  // namespace quic
