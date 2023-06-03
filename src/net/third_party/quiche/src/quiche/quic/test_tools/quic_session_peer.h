// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_

#include <cstdint>
#include <map>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_write_blocked_list.h"

namespace quic {

class QuicCryptoStream;
class QuicSession;
class QuicStream;

namespace test {

class QuicSessionPeer {
 public:
  QuicSessionPeer() = delete;

  static QuicStreamId GetNextOutgoingBidirectionalStreamId(
      QuicSession* session);
  static QuicStreamId GetNextOutgoingUnidirectionalStreamId(
      QuicSession* session);
  static void SetNextOutgoingBidirectionalStreamId(QuicSession* session,
                                                   QuicStreamId id);
  // Following is only for Google-QUIC, will QUIC_BUG if called for IETF
  // QUIC.
  static void SetMaxOpenIncomingStreams(QuicSession* session,
                                        uint32_t max_streams);
  // Following two are only for IETF-QUIC, will QUIC_BUG if called for Google
  // QUIC.
  static void SetMaxOpenIncomingBidirectionalStreams(QuicSession* session,
                                                     uint32_t max_streams);
  static void SetMaxOpenIncomingUnidirectionalStreams(QuicSession* session,
                                                      uint32_t max_streams);

  static void SetMaxOpenOutgoingStreams(QuicSession* session,
                                        uint32_t max_streams);
  static void SetMaxOpenOutgoingBidirectionalStreams(QuicSession* session,
                                                     uint32_t max_streams);
  static void SetMaxOpenOutgoingUnidirectionalStreams(QuicSession* session,
                                                      uint32_t max_streams);

  static QuicCryptoStream* GetMutableCryptoStream(QuicSession* session);
  static QuicWriteBlockedListInterface* GetWriteBlockedStreams(
      QuicSession* session);
  static QuicStream* GetOrCreateStream(QuicSession* session,
                                       QuicStreamId stream_id);
  static absl::flat_hash_map<QuicStreamId, QuicStreamOffset>&
  GetLocallyClosedStreamsHighestOffset(QuicSession* session);
  static QuicSession::StreamMap& stream_map(QuicSession* session);
  static const QuicSession::ClosedStreams& closed_streams(QuicSession* session);
  static void ActivateStream(QuicSession* session,
                             std::unique_ptr<QuicStream> stream);

  // Discern the state of a stream.  Exactly one of these should be true at a
  // time for any stream id > 0 (other than the special streams 1 and 3).
  static bool IsStreamClosed(QuicSession* session, QuicStreamId id);
  static bool IsStreamCreated(QuicSession* session, QuicStreamId id);
  static bool IsStreamAvailable(QuicSession* session, QuicStreamId id);

  static QuicStream* GetStream(QuicSession* session, QuicStreamId id);
  static bool IsStreamWriteBlocked(QuicSession* session, QuicStreamId id);
  static QuicAlarm* GetCleanUpClosedStreamsAlarm(QuicSession* session);
  static LegacyQuicStreamIdManager* GetStreamIdManager(QuicSession* session);
  static UberQuicStreamIdManager* ietf_streamid_manager(QuicSession* session);
  static QuicStreamIdManager* ietf_bidirectional_stream_id_manager(
      QuicSession* session);
  static QuicStreamIdManager* ietf_unidirectional_stream_id_manager(
      QuicSession* session);
  static PendingStream* GetPendingStream(QuicSession* session,
                                         QuicStreamId stream_id);
  static void set_is_configured(QuicSession* session, bool value);
  static void SetPerspective(QuicSession* session, Perspective perspective);
  static size_t GetNumOpenDynamicStreams(QuicSession* session);
  static size_t GetNumDrainingStreams(QuicSession* session);
  static QuicStreamId GetLargestPeerCreatedStreamId(QuicSession* session,
                                                    bool unidirectional) {
    return session->GetLargestPeerCreatedStreamId(unidirectional);
  }
};

}  // namespace test

}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_SESSION_PEER_H_
