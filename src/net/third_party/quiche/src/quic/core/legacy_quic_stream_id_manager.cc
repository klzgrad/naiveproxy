// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quiche/src/quic/core/legacy_quic_stream_id_manager.h"

#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

LegacyQuicStreamIdManager::LegacyQuicStreamIdManager(
    Perspective perspective,
    QuicTransportVersion transport_version,
    size_t max_open_outgoing_streams,
    size_t max_open_incoming_streams)
    : perspective_(perspective),
      transport_version_(transport_version),
      max_open_outgoing_streams_(max_open_outgoing_streams),
      max_open_incoming_streams_(max_open_incoming_streams),
      next_outgoing_stream_id_(
          QuicUtils::GetFirstBidirectionalStreamId(transport_version_,
                                                   perspective_)),
      largest_peer_created_stream_id_(
          perspective_ == Perspective::IS_SERVER
              ? (QuicVersionUsesCryptoFrames(transport_version_)
                     ? QuicUtils::GetInvalidStreamId(transport_version_)
                     : QuicUtils::GetCryptoStreamId(transport_version_))
              : QuicUtils::GetInvalidStreamId(transport_version_)) {}

LegacyQuicStreamIdManager::~LegacyQuicStreamIdManager() {}

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
          QuicUtils::GetInvalidStreamId(transport_version_) &&
      stream_id <= largest_peer_created_stream_id_) {
    return true;
  }

  // Check if the new number of available streams would cause the number of
  // available streams to exceed the limit.  Note that the peer can create
  // only alternately-numbered streams.
  size_t additional_available_streams =
      (stream_id - largest_peer_created_stream_id_) / 2 - 1;
  if (largest_peer_created_stream_id_ ==
      QuicUtils::GetInvalidStreamId(transport_version_)) {
    additional_available_streams = (stream_id + 1) / 2 - 1;
  }
  size_t new_num_available_streams =
      GetNumAvailableStreams() + additional_available_streams;
  if (new_num_available_streams > MaxAvailableStreams()) {
    QUIC_DLOG(INFO) << perspective_
                    << "Failed to create a new incoming stream with id:"
                    << stream_id << ".  There are already "
                    << GetNumAvailableStreams()
                    << " streams available, which would become "
                    << new_num_available_streams << ", which exceeds the limit "
                    << MaxAvailableStreams() << ".";
    return false;
  }
  QuicStreamId first_available_stream = largest_peer_created_stream_id_ + 2;
  if (largest_peer_created_stream_id_ ==
      QuicUtils::GetInvalidStreamId(transport_version_)) {
    first_available_stream = QuicUtils::GetFirstBidirectionalStreamId(
        transport_version_, QuicUtils::InvertPerspective(perspective_));
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
             QuicUtils::GetInvalidStreamId(transport_version_) ||
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
