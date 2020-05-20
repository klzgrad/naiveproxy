// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/uber_quic_stream_id_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"

namespace quic {

UberQuicStreamIdManager::UberQuicStreamIdManager(
    Perspective perspective,
    ParsedQuicVersion version,
    QuicStreamIdManager::DelegateInterface* delegate,
    QuicStreamCount max_open_outgoing_bidirectional_streams,
    QuicStreamCount max_open_outgoing_unidirectional_streams,
    QuicStreamCount max_open_incoming_bidirectional_streams,
    QuicStreamCount max_open_incoming_unidirectional_streams)
    : bidirectional_stream_id_manager_(delegate,
                                       /*unidirectional=*/false,
                                       perspective,
                                       version.transport_version,
                                       max_open_outgoing_bidirectional_streams,
                                       max_open_incoming_bidirectional_streams),
      unidirectional_stream_id_manager_(
          delegate,
          /*unidirectional=*/true,
          perspective,
          version.transport_version,
          max_open_outgoing_unidirectional_streams,
          max_open_incoming_unidirectional_streams) {}

bool UberQuicStreamIdManager::MaybeAllowNewOutgoingBidirectionalStreams(
    QuicStreamCount max_open_streams) {
  return bidirectional_stream_id_manager_.MaybeAllowNewOutgoingStreams(
      max_open_streams);
}
bool UberQuicStreamIdManager::MaybeAllowNewOutgoingUnidirectionalStreams(
    QuicStreamCount max_open_streams) {
  return unidirectional_stream_id_manager_.MaybeAllowNewOutgoingStreams(
      max_open_streams);
}
void UberQuicStreamIdManager::SetMaxOpenIncomingBidirectionalStreams(
    QuicStreamCount max_open_streams) {
  bidirectional_stream_id_manager_.SetMaxOpenIncomingStreams(max_open_streams);
}
void UberQuicStreamIdManager::SetMaxOpenIncomingUnidirectionalStreams(
    QuicStreamCount max_open_streams) {
  unidirectional_stream_id_manager_.SetMaxOpenIncomingStreams(max_open_streams);
}

bool UberQuicStreamIdManager::CanOpenNextOutgoingBidirectionalStream() const {
  return bidirectional_stream_id_manager_.CanOpenNextOutgoingStream();
}

bool UberQuicStreamIdManager::CanOpenNextOutgoingUnidirectionalStream() const {
  return unidirectional_stream_id_manager_.CanOpenNextOutgoingStream();
}

QuicStreamId UberQuicStreamIdManager::GetNextOutgoingBidirectionalStreamId() {
  return bidirectional_stream_id_manager_.GetNextOutgoingStreamId();
}

QuicStreamId UberQuicStreamIdManager::GetNextOutgoingUnidirectionalStreamId() {
  return unidirectional_stream_id_manager_.GetNextOutgoingStreamId();
}

bool UberQuicStreamIdManager::MaybeIncreaseLargestPeerStreamId(
    QuicStreamId id,
    std::string* error_details) {
  if (QuicUtils::IsBidirectionalStreamId(id)) {
    return bidirectional_stream_id_manager_.MaybeIncreaseLargestPeerStreamId(
        id, error_details);
  }
  return unidirectional_stream_id_manager_.MaybeIncreaseLargestPeerStreamId(
      id, error_details);
}

void UberQuicStreamIdManager::OnStreamClosed(QuicStreamId id) {
  if (QuicUtils::IsBidirectionalStreamId(id)) {
    bidirectional_stream_id_manager_.OnStreamClosed(id);
    return;
  }
  unidirectional_stream_id_manager_.OnStreamClosed(id);
}

bool UberQuicStreamIdManager::OnStreamsBlockedFrame(
    const QuicStreamsBlockedFrame& frame,
    std::string* error_details) {
  if (frame.unidirectional) {
    return unidirectional_stream_id_manager_.OnStreamsBlockedFrame(
        frame, error_details);
  }
  return bidirectional_stream_id_manager_.OnStreamsBlockedFrame(frame,
                                                                error_details);
}

bool UberQuicStreamIdManager::IsIncomingStream(QuicStreamId id) const {
  if (QuicUtils::IsBidirectionalStreamId(id)) {
    return bidirectional_stream_id_manager_.IsIncomingStream(id);
  }
  return unidirectional_stream_id_manager_.IsIncomingStream(id);
}

bool UberQuicStreamIdManager::IsAvailableStream(QuicStreamId id) const {
  if (QuicUtils::IsBidirectionalStreamId(id)) {
    return bidirectional_stream_id_manager_.IsAvailableStream(id);
  }
  return unidirectional_stream_id_manager_.IsAvailableStream(id);
}

QuicStreamCount
UberQuicStreamIdManager::GetMaxAllowdIncomingBidirectionalStreams() const {
  return bidirectional_stream_id_manager_.incoming_initial_max_open_streams();
}

QuicStreamCount
UberQuicStreamIdManager::GetMaxAllowdIncomingUnidirectionalStreams() const {
  return unidirectional_stream_id_manager_.incoming_initial_max_open_streams();
}

QuicStreamId UberQuicStreamIdManager::GetLargestPeerCreatedStreamId(
    bool unidirectional) const {
  if (unidirectional) {
    return unidirectional_stream_id_manager_.largest_peer_created_stream_id();
  }
  return bidirectional_stream_id_manager_.largest_peer_created_stream_id();
}

QuicStreamId UberQuicStreamIdManager::next_outgoing_bidirectional_stream_id()
    const {
  return bidirectional_stream_id_manager_.next_outgoing_stream_id();
}

QuicStreamId UberQuicStreamIdManager::next_outgoing_unidirectional_stream_id()
    const {
  return unidirectional_stream_id_manager_.next_outgoing_stream_id();
}

QuicStreamCount UberQuicStreamIdManager::max_outgoing_bidirectional_streams()
    const {
  return bidirectional_stream_id_manager_.outgoing_max_streams();
}

QuicStreamCount UberQuicStreamIdManager::max_outgoing_unidirectional_streams()
    const {
  return unidirectional_stream_id_manager_.outgoing_max_streams();
}

QuicStreamCount UberQuicStreamIdManager::max_incoming_bidirectional_streams()
    const {
  return bidirectional_stream_id_manager_.incoming_actual_max_streams();
}

QuicStreamCount UberQuicStreamIdManager::max_incoming_unidirectional_streams()
    const {
  return unidirectional_stream_id_manager_.incoming_actual_max_streams();
}

QuicStreamCount
UberQuicStreamIdManager::advertised_max_incoming_bidirectional_streams() const {
  return bidirectional_stream_id_manager_.incoming_advertised_max_streams();
}

QuicStreamCount
UberQuicStreamIdManager::advertised_max_incoming_unidirectional_streams()
    const {
  return unidirectional_stream_id_manager_.incoming_advertised_max_streams();
}

}  // namespace quic
