// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/core/uber_quic_stream_id_manager.h"

#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/core/quic_utils.h"

namespace quic {
namespace {

Perspective Reverse(Perspective perspective) {
  return perspective == Perspective::IS_SERVER ? Perspective::IS_CLIENT
                                               : Perspective::IS_SERVER;
}

}  // namespace

UberQuicStreamIdManager::UberQuicStreamIdManager(
    QuicSession* session,
    size_t max_open_outgoing_streams,
    size_t max_open_incoming_streams)
    : bidirectional_stream_id_manager_(
          session,
          QuicUtils::GetFirstBidirectionalStreamId(
              session->connection()->transport_version(),
              session->perspective()),
          session->perspective() == Perspective::IS_SERVER
              ? QuicUtils::GetCryptoStreamId(
                    session->connection()->transport_version())
              : QuicUtils::GetInvalidStreamId(
                    session->connection()->transport_version()),
          QuicUtils::GetFirstBidirectionalStreamId(
              session->connection()->transport_version(),
              Reverse(session->perspective())),
          max_open_outgoing_streams,
          max_open_incoming_streams),
      unidirectional_stream_id_manager_(
          session,
          QuicUtils::GetFirstUnidirectionalStreamId(
              session->connection()->transport_version(),
              session->perspective()),
          QuicUtils::GetInvalidStreamId(
              session->connection()->transport_version()),
          QuicUtils::GetFirstUnidirectionalStreamId(
              session->connection()->transport_version(),
              Reverse(session->perspective())),
          max_open_outgoing_streams,
          max_open_incoming_streams) {}

void UberQuicStreamIdManager::RegisterStaticStream(QuicStreamId id) {
  if (QuicUtils::IsBidirectionalStreamId(id)) {
    bidirectional_stream_id_manager_.RegisterStaticStream(id);
    return;
  }
  unidirectional_stream_id_manager_.RegisterStaticStream(id);
}

void UberQuicStreamIdManager::SetMaxOpenOutgoingStreams(size_t max_streams) {
  bidirectional_stream_id_manager_.SetMaxOpenOutgoingStreams(max_streams);
  unidirectional_stream_id_manager_.SetMaxOpenOutgoingStreams(max_streams);
}

void UberQuicStreamIdManager::SetMaxOpenIncomingStreams(size_t max_streams) {
  bidirectional_stream_id_manager_.SetMaxOpenIncomingStreams(max_streams);
  unidirectional_stream_id_manager_.SetMaxOpenIncomingStreams(max_streams);
}

bool UberQuicStreamIdManager::CanOpenNextOutgoingBidirectionalStream() {
  return bidirectional_stream_id_manager_.CanOpenNextOutgoingStream();
}

bool UberQuicStreamIdManager::CanOpenNextOutgoingUnidirectionalStream() {
  return unidirectional_stream_id_manager_.CanOpenNextOutgoingStream();
}

QuicStreamId UberQuicStreamIdManager::GetNextOutgoingBidirectionalStreamId() {
  return bidirectional_stream_id_manager_.GetNextOutgoingStreamId();
}

QuicStreamId UberQuicStreamIdManager::GetNextOutgoingUnidirectionalStreamId() {
  return unidirectional_stream_id_manager_.GetNextOutgoingStreamId();
}

bool UberQuicStreamIdManager::MaybeIncreaseLargestPeerStreamId(
    QuicStreamId id) {
  if (QuicUtils::IsBidirectionalStreamId(id)) {
    return bidirectional_stream_id_manager_.MaybeIncreaseLargestPeerStreamId(
        id);
  }
  return unidirectional_stream_id_manager_.MaybeIncreaseLargestPeerStreamId(id);
}

void UberQuicStreamIdManager::OnStreamClosed(QuicStreamId id) {
  if (QuicUtils::IsBidirectionalStreamId(id)) {
    bidirectional_stream_id_manager_.OnStreamClosed(id);
    return;
  }
  unidirectional_stream_id_manager_.OnStreamClosed(id);
}

bool UberQuicStreamIdManager::OnMaxStreamIdFrame(
    const QuicMaxStreamIdFrame& frame) {
  if (QuicUtils::IsBidirectionalStreamId(frame.max_stream_id)) {
    return bidirectional_stream_id_manager_.OnMaxStreamIdFrame(frame);
  }
  return unidirectional_stream_id_manager_.OnMaxStreamIdFrame(frame);
}

bool UberQuicStreamIdManager::OnStreamIdBlockedFrame(
    const QuicStreamIdBlockedFrame& frame) {
  if (QuicUtils::IsBidirectionalStreamId(frame.stream_id)) {
    return bidirectional_stream_id_manager_.OnStreamIdBlockedFrame(frame);
  }
  return unidirectional_stream_id_manager_.OnStreamIdBlockedFrame(frame);
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

size_t UberQuicStreamIdManager::GetMaxAllowdIncomingBidirectionalStreams()
    const {
  return bidirectional_stream_id_manager_.max_allowed_incoming_streams();
}

size_t UberQuicStreamIdManager::GetMaxAllowdIncomingUnidirectionalStreams()
    const {
  return unidirectional_stream_id_manager_.max_allowed_incoming_streams();
}

void UberQuicStreamIdManager::SetLargestPeerCreatedStreamId(
    QuicStreamId largest_peer_created_stream_id) {
  if (QuicUtils::IsBidirectionalStreamId(largest_peer_created_stream_id)) {
    bidirectional_stream_id_manager_.set_largest_peer_created_stream_id(
        largest_peer_created_stream_id);
    return;
  }
  unidirectional_stream_id_manager_.set_largest_peer_created_stream_id(
      largest_peer_created_stream_id);
}

QuicStreamId UberQuicStreamIdManager::next_outgoing_bidirectional_stream_id()
    const {
  return bidirectional_stream_id_manager_.next_outgoing_stream_id();
}

QuicStreamId UberQuicStreamIdManager::next_outgoing_unidirectional_stream_id()
    const {
  return unidirectional_stream_id_manager_.next_outgoing_stream_id();
}

QuicStreamId
UberQuicStreamIdManager::max_allowed_outgoing_bidirectional_stream_id() const {
  return bidirectional_stream_id_manager_.max_allowed_outgoing_stream_id();
}

QuicStreamId
UberQuicStreamIdManager::max_allowed_outgoing_unidirectional_stream_id() const {
  return unidirectional_stream_id_manager_.max_allowed_outgoing_stream_id();
}

size_t UberQuicStreamIdManager::max_allowed_outgoing_bidirectional_streams()
    const {
  return bidirectional_stream_id_manager_.max_allowed_outgoing_streams();
}

size_t UberQuicStreamIdManager::max_allowed_outgoing_unidirectional_streams()
    const {
  return unidirectional_stream_id_manager_.max_allowed_outgoing_streams();
}

QuicStreamId
UberQuicStreamIdManager::actual_max_allowed_incoming_bidirectional_stream_id()
    const {
  return bidirectional_stream_id_manager_
      .actual_max_allowed_incoming_stream_id();
}

QuicStreamId
UberQuicStreamIdManager::actual_max_allowed_incoming_unidirectional_stream_id()
    const {
  return unidirectional_stream_id_manager_
      .actual_max_allowed_incoming_stream_id();
}

QuicStreamId UberQuicStreamIdManager::
    advertised_max_allowed_incoming_bidirectional_stream_id() const {
  return bidirectional_stream_id_manager_
      .advertised_max_allowed_incoming_stream_id();
}

QuicStreamId UberQuicStreamIdManager::
    advertised_max_allowed_incoming_unidirectional_stream_id() const {
  return unidirectional_stream_id_manager_
      .advertised_max_allowed_incoming_stream_id();
}

}  // namespace quic
