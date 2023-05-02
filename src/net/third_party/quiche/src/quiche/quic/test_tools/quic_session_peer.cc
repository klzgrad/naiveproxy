// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_session_peer.h"

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_utils.h"

namespace quic {
namespace test {

// static
QuicStreamId QuicSessionPeer::GetNextOutgoingBidirectionalStreamId(
    QuicSession* session) {
  return session->GetNextOutgoingBidirectionalStreamId();
}

// static
QuicStreamId QuicSessionPeer::GetNextOutgoingUnidirectionalStreamId(
    QuicSession* session) {
  return session->GetNextOutgoingUnidirectionalStreamId();
}

// static
void QuicSessionPeer::SetNextOutgoingBidirectionalStreamId(QuicSession* session,
                                                           QuicStreamId id) {
  if (VersionHasIetfQuicFrames(session->transport_version())) {
    session->ietf_streamid_manager_.bidirectional_stream_id_manager_
        .next_outgoing_stream_id_ = id;
    return;
  }
  session->stream_id_manager_.next_outgoing_stream_id_ = id;
}

// static
void QuicSessionPeer::SetMaxOpenIncomingStreams(QuicSession* session,
                                                uint32_t max_streams) {
  if (VersionHasIetfQuicFrames(session->transport_version())) {
    QUIC_BUG(quic_bug_10193_1)
        << "SetmaxOpenIncomingStreams deprecated for IETF QUIC";
    session->ietf_streamid_manager_.SetMaxOpenIncomingUnidirectionalStreams(
        max_streams);
    session->ietf_streamid_manager_.SetMaxOpenIncomingBidirectionalStreams(
        max_streams);
    return;
  }
  session->stream_id_manager_.set_max_open_incoming_streams(max_streams);
}

// static
void QuicSessionPeer::SetMaxOpenIncomingBidirectionalStreams(
    QuicSession* session, uint32_t max_streams) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(session->transport_version()))
      << "SetmaxOpenIncomingBidirectionalStreams not supported for Google "
         "QUIC";
  session->ietf_streamid_manager_.SetMaxOpenIncomingBidirectionalStreams(
      max_streams);
}
// static
void QuicSessionPeer::SetMaxOpenIncomingUnidirectionalStreams(
    QuicSession* session, uint32_t max_streams) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(session->transport_version()))
      << "SetmaxOpenIncomingUnidirectionalStreams not supported for Google "
         "QUIC";
  session->ietf_streamid_manager_.SetMaxOpenIncomingUnidirectionalStreams(
      max_streams);
}

// static
void QuicSessionPeer::SetMaxOpenOutgoingStreams(QuicSession* session,
                                                uint32_t max_streams) {
  if (VersionHasIetfQuicFrames(session->transport_version())) {
    QUIC_BUG(quic_bug_10193_2)
        << "SetmaxOpenOutgoingStreams deprecated for IETF QUIC";
    return;
  }
  session->stream_id_manager_.set_max_open_outgoing_streams(max_streams);
}

// static
void QuicSessionPeer::SetMaxOpenOutgoingBidirectionalStreams(
    QuicSession* session, uint32_t max_streams) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(session->transport_version()))
      << "SetmaxOpenOutgoingBidirectionalStreams not supported for Google "
         "QUIC";
  session->ietf_streamid_manager_.MaybeAllowNewOutgoingBidirectionalStreams(
      max_streams);
}
// static
void QuicSessionPeer::SetMaxOpenOutgoingUnidirectionalStreams(
    QuicSession* session, uint32_t max_streams) {
  QUICHE_DCHECK(VersionHasIetfQuicFrames(session->transport_version()))
      << "SetmaxOpenOutgoingUnidirectionalStreams not supported for Google "
         "QUIC";
  session->ietf_streamid_manager_.MaybeAllowNewOutgoingUnidirectionalStreams(
      max_streams);
}

// static
QuicCryptoStream* QuicSessionPeer::GetMutableCryptoStream(
    QuicSession* session) {
  return session->GetMutableCryptoStream();
}

// static
QuicWriteBlockedList* QuicSessionPeer::GetWriteBlockedStreams(
    QuicSession* session) {
  return &session->write_blocked_streams_;
}

// static
QuicStream* QuicSessionPeer::GetOrCreateStream(QuicSession* session,
                                               QuicStreamId stream_id) {
  return session->GetOrCreateStream(stream_id);
}

// static
absl::flat_hash_map<QuicStreamId, QuicStreamOffset>&
QuicSessionPeer::GetLocallyClosedStreamsHighestOffset(QuicSession* session) {
  return session->locally_closed_streams_highest_offset_;
}

// static
QuicSession::StreamMap& QuicSessionPeer::stream_map(QuicSession* session) {
  return session->stream_map_;
}

// static
const QuicSession::ClosedStreams& QuicSessionPeer::closed_streams(
    QuicSession* session) {
  return *session->closed_streams();
}

// static
void QuicSessionPeer::ActivateStream(QuicSession* session,
                                     std::unique_ptr<QuicStream> stream) {
  return session->ActivateStream(std::move(stream));
}

// static
bool QuicSessionPeer::IsStreamClosed(QuicSession* session, QuicStreamId id) {
  return session->IsClosedStream(id);
}

// static
bool QuicSessionPeer::IsStreamCreated(QuicSession* session, QuicStreamId id) {
  return session->stream_map_.contains(id);
}

// static
bool QuicSessionPeer::IsStreamAvailable(QuicSession* session, QuicStreamId id) {
  if (VersionHasIetfQuicFrames(session->transport_version())) {
    if (id % QuicUtils::StreamIdDelta(session->transport_version()) < 2) {
      return session->ietf_streamid_manager_.bidirectional_stream_id_manager_
          .available_streams_.contains(id);
    }
    return session->ietf_streamid_manager_.unidirectional_stream_id_manager_
        .available_streams_.contains(id);
  }
  return session->stream_id_manager_.available_streams_.contains(id);
}

// static
QuicStream* QuicSessionPeer::GetStream(QuicSession* session, QuicStreamId id) {
  return session->GetStream(id);
}

// static
bool QuicSessionPeer::IsStreamWriteBlocked(QuicSession* session,
                                           QuicStreamId id) {
  return session->write_blocked_streams_.IsStreamBlocked(id);
}

// static
QuicAlarm* QuicSessionPeer::GetCleanUpClosedStreamsAlarm(QuicSession* session) {
  return session->closed_streams_clean_up_alarm_.get();
}

// static
LegacyQuicStreamIdManager* QuicSessionPeer::GetStreamIdManager(
    QuicSession* session) {
  return &session->stream_id_manager_;
}

// static
UberQuicStreamIdManager* QuicSessionPeer::ietf_streamid_manager(
    QuicSession* session) {
  return &session->ietf_streamid_manager_;
}

// static
QuicStreamIdManager* QuicSessionPeer::ietf_bidirectional_stream_id_manager(
    QuicSession* session) {
  return &session->ietf_streamid_manager_.bidirectional_stream_id_manager_;
}

// static
QuicStreamIdManager* QuicSessionPeer::ietf_unidirectional_stream_id_manager(
    QuicSession* session) {
  return &session->ietf_streamid_manager_.unidirectional_stream_id_manager_;
}

// static
PendingStream* QuicSessionPeer::GetPendingStream(QuicSession* session,
                                                 QuicStreamId stream_id) {
  auto it = session->pending_stream_map_.find(stream_id);
  return it == session->pending_stream_map_.end() ? nullptr : it->second.get();
}

// static
void QuicSessionPeer::set_is_configured(QuicSession* session, bool value) {
  session->is_configured_ = value;
}

// static
void QuicSessionPeer::SetPerspective(QuicSession* session,
                                     Perspective perspective) {
  session->perspective_ = perspective;
}

// static
size_t QuicSessionPeer::GetNumOpenDynamicStreams(QuicSession* session) {
  size_t result = 0;
  for (const auto& it : session->stream_map_) {
    if (!it.second->is_static()) {
      ++result;
    }
  }
  // Exclude draining streams.
  result -= session->num_draining_streams_;
  // Add locally closed streams.
  result += session->locally_closed_streams_highest_offset_.size();

  return result;
}

// static
size_t QuicSessionPeer::GetNumDrainingStreams(QuicSession* session) {
  return session->num_draining_streams_;
}

}  // namespace test
}  // namespace quic
