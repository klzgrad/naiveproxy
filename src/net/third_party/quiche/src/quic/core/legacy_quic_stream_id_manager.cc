// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quiche/src/quic/core/legacy_quic_stream_id_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"

namespace quic {

#define ENDPOINT \
  (session_->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

LegacyQuicStreamIdManager::LegacyQuicStreamIdManager(
    QuicSession* session,
    size_t max_open_outgoing_streams,
    size_t max_open_incoming_streams)
    : session_(session),
      max_open_outgoing_streams_(max_open_outgoing_streams),
      max_open_incoming_streams_(max_open_incoming_streams),
      next_outgoing_stream_id_(
          QuicUtils::GetFirstBidirectionalStreamId(session->transport_version(),
                                                   session->perspective())),
      largest_peer_created_stream_id_(
          session->perspective() == Perspective::IS_SERVER
              ? (QuicVersionUsesCryptoFrames(session->transport_version())
                     ? QuicUtils::GetInvalidStreamId(
                           session->transport_version())
                     : QuicUtils::GetCryptoStreamId(
                           session->transport_version()))
              : QuicUtils::GetInvalidStreamId(session->transport_version())) {}

LegacyQuicStreamIdManager::~LegacyQuicStreamIdManager() {
  QUIC_LOG_IF(WARNING,
              session_->num_locally_closed_incoming_streams_highest_offset() >
                  max_open_incoming_streams_)
      << "Surprisingly high number of locally closed peer initiated streams"
         "still waiting for final byte offset: "
      << session_->num_locally_closed_incoming_streams_highest_offset();
  QUIC_LOG_IF(WARNING,
              session_->GetNumLocallyClosedOutgoingStreamsHighestOffset() >
                  max_open_outgoing_streams_)
      << "Surprisingly high number of locally closed self initiated streams"
         "still waiting for final byte offset: "
      << session_->GetNumLocallyClosedOutgoingStreamsHighestOffset();
}

bool LegacyQuicStreamIdManager::CanOpenNextOutgoingStream(
    size_t current_num_open_outgoing_streams) const {
  if (current_num_open_outgoing_streams >= max_open_outgoing_streams_) {
    QUIC_DLOG(INFO) << "Failed to create a new outgoing stream. "
                    << "Already " << current_num_open_outgoing_streams
                    << " open.";
    return false;
  }
  return true;
}

bool LegacyQuicStreamIdManager::CanOpenIncomingStream(
    size_t current_num_open_incoming_streams) const {
  // Check if the new number of open streams would cause the number of
  // open streams to exceed the limit.
  return current_num_open_incoming_streams < max_open_incoming_streams_;
}

bool LegacyQuicStreamIdManager::MaybeIncreaseLargestPeerStreamId(
    const QuicStreamId stream_id) {
  available_streams_.erase(stream_id);

  if (largest_peer_created_stream_id_ !=
          QuicUtils::GetInvalidStreamId(session_->transport_version()) &&
      stream_id <= largest_peer_created_stream_id_) {
    return true;
  }

  // Check if the new number of available streams would cause the number of
  // available streams to exceed the limit.  Note that the peer can create
  // only alternately-numbered streams.
  size_t additional_available_streams =
      (stream_id - largest_peer_created_stream_id_) / 2 - 1;
  if (largest_peer_created_stream_id_ ==
      QuicUtils::GetInvalidStreamId(session_->transport_version())) {
    additional_available_streams = (stream_id + 1) / 2 - 1;
  }
  size_t new_num_available_streams =
      GetNumAvailableStreams() + additional_available_streams;
  if (new_num_available_streams > MaxAvailableStreams()) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Failed to create a new incoming stream with id:"
                    << stream_id << ".  There are already "
                    << GetNumAvailableStreams()
                    << " streams available, which would become "
                    << new_num_available_streams << ", which exceeds the limit "
                    << MaxAvailableStreams() << ".";
    session_->connection()->CloseConnection(
        QUIC_TOO_MANY_AVAILABLE_STREAMS,
        QuicStrCat(new_num_available_streams, " above ", MaxAvailableStreams()),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  QuicStreamId first_available_stream = largest_peer_created_stream_id_ + 2;
  if (largest_peer_created_stream_id_ ==
      QuicUtils::GetInvalidStreamId(session_->transport_version())) {
    first_available_stream = QuicUtils::GetFirstBidirectionalStreamId(
        session_->transport_version(),
        QuicUtils::InvertPerspective(session_->perspective()));
  }
  for (QuicStreamId id = first_available_stream; id < stream_id; id += 2) {
    available_streams_.insert(id);
  }
  largest_peer_created_stream_id_ = stream_id;

  return true;
}

QuicStreamId LegacyQuicStreamIdManager::GetNextOutgoingStreamId() {
  QuicStreamId id = next_outgoing_stream_id_;
  next_outgoing_stream_id_ += 2;
  return id;
}

bool LegacyQuicStreamIdManager::IsAvailableStream(QuicStreamId id) const {
  if (!IsIncomingStream(id)) {
    // Stream IDs under next_ougoing_stream_id_ are either open or previously
    // open but now closed.
    return id >= next_outgoing_stream_id_;
  }
  // For peer created streams, we also need to consider available streams.
  return largest_peer_created_stream_id_ ==
             QuicUtils::GetInvalidStreamId(session_->transport_version()) ||
         id > largest_peer_created_stream_id_ ||
         QuicContainsKey(available_streams_, id);
}

bool LegacyQuicStreamIdManager::IsIncomingStream(QuicStreamId id) const {
  return id % 2 != next_outgoing_stream_id_ % 2;
}

size_t LegacyQuicStreamIdManager::GetNumAvailableStreams() const {
  return available_streams_.size();
}

size_t LegacyQuicStreamIdManager::MaxAvailableStreams() const {
  return max_open_incoming_streams_ * kMaxAvailableStreamsMultiplier;
}

}  // namespace quic
