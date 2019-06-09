// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"

namespace quic {

class QuicHeadersStream;
class QuicSpdySession;
class QuicHpackDebugVisitor;

namespace test {

class QuicSpdySessionPeer {
 public:
  QuicSpdySessionPeer() = delete;

  static QuicHeadersStream* GetHeadersStream(QuicSpdySession* session);
  static void SetHeadersStream(QuicSpdySession* session,
                               QuicHeadersStream* headers_stream);
  static void SetUnownedHeadersStream(QuicSpdySession* session,
                                      QuicHeadersStream* headers_stream);
  static const spdy::SpdyFramer& GetSpdyFramer(QuicSpdySession* session);
  static void SetHpackEncoderDebugVisitor(
      QuicSpdySession* session,
      std::unique_ptr<QuicHpackDebugVisitor> visitor);
  static void SetHpackDecoderDebugVisitor(
      QuicSpdySession* session,
      std::unique_ptr<QuicHpackDebugVisitor> visitor);
  static void SetMaxUncompressedHeaderBytes(
      QuicSpdySession* session,
      size_t set_max_uncompressed_header_bytes);
  static size_t WriteHeadersOnHeadersStream(
      QuicSpdySession* session,
      QuicStreamId id,
      spdy::SpdyHeaderBlock headers,
      bool fin,
      spdy::SpdyPriority priority,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);
  // |session| can't be nullptr.
  static QuicStreamId GetNextOutgoingUnidirectionalStreamId(
      QuicSpdySession* session);
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
