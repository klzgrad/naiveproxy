// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_

#include "base/macros.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/core/quic_write_blocked_list.h"
#include "net/third_party/spdy/core/spdy_framer.h"

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
  static size_t WriteHeadersImpl(
      QuicSpdySession* session,
      QuicStreamId id,
      spdy::SpdyHeaderBlock headers,
      bool fin,
      int weight,
      QuicStreamId parent_stream_id,
      bool exclusive,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);
  // Helper functions for stream ids, to allow test logic to abstract
  // over the HTTP stream numbering scheme (i.e. whether one or
  // two QUIC streams are used per HTTP transaction).
  static QuicStreamId NextStreamId(const QuicSpdySession& session);
  // n should start at 0.
  static QuicStreamId GetNthClientInitiatedStreamId(
      const QuicSpdySession& session,
      int n);
  // n should start at 0.
  static QuicStreamId GetNthServerInitiatedStreamId(
      const QuicSpdySession& session,
      int n);
};

}  // namespace test

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
