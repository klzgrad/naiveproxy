// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_write_blocked_list.h"
#include "net/spdy/core/spdy_framer.h"

namespace net {

class QuicHeadersStream;
class QuicSpdySession;
class QuicHpackDebugVisitor;

namespace test {

class QuicSpdySessionPeer {
 public:
  static QuicHeadersStream* GetHeadersStream(QuicSpdySession* session);
  static void SetHeadersStream(QuicSpdySession* session,
                               QuicHeadersStream* headers_stream);
  static const SpdyFramer& GetSpdyFramer(QuicSpdySession* session);
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
      SpdyHeaderBlock headers,
      bool fin,
      SpdyPriority priority,
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

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicSpdySessionPeer);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_SPDY_SESSION_PEER_H_
