// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_
#define NET_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_

#include <cstdint>
#include <map>
#include <memory>

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_session.h"
#include "net/quic/core/quic_write_blocked_list.h"
#include "net/quic/platform/api/quic_containers.h"

namespace net {

class QuicCryptoStream;
class QuicSession;
class QuicStream;

namespace test {

class QuicSessionPeer {
 public:
  static QuicStreamId GetNextOutgoingStreamId(QuicSession* session);
  static void SetNextOutgoingStreamId(QuicSession* session, QuicStreamId id);
  static void SetMaxOpenIncomingStreams(QuicSession* session,
                                        uint32_t max_streams);
  static void SetMaxOpenOutgoingStreams(QuicSession* session,
                                        uint32_t max_streams);
  static QuicCryptoStream* GetMutableCryptoStream(QuicSession* session);
  static QuicWriteBlockedList* GetWriteBlockedStreams(QuicSession* session);
  static QuicStream* GetOrCreateDynamicStream(QuicSession* session,
                                              QuicStreamId stream_id);
  static std::map<QuicStreamId, QuicStreamOffset>&
  GetLocallyClosedStreamsHighestOffset(QuicSession* session);
  static QuicSession::StaticStreamMap& static_streams(QuicSession* session);
  static QuicSession::DynamicStreamMap& dynamic_streams(QuicSession* session);
  static const QuicSession::ClosedStreams& closed_streams(QuicSession* session);
  static QuicSession::ZombieStreamMap& zombie_streams(QuicSession* session);
  static QuicUnorderedSet<QuicStreamId>* GetDrainingStreams(
      QuicSession* session);
  static void ActivateStream(QuicSession* session,
                             std::unique_ptr<QuicStream> stream);

  // Discern the state of a stream.  Exactly one of these should be true at a
  // time for any stream id > 0 (other than the special streams 1 and 3).
  static bool IsStreamClosed(QuicSession* session, QuicStreamId id);
  static bool IsStreamCreated(QuicSession* session, QuicStreamId id);
  static bool IsStreamAvailable(QuicSession* session, QuicStreamId id);
  static bool IsStreamUncreated(QuicSession* session, QuicStreamId id);

  static QuicStream* GetStream(QuicSession* session, QuicStreamId id);

 private:
  DISALLOW_COPY_AND_ASSIGN(QuicSessionPeer);
};

}  // namespace test

}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_
