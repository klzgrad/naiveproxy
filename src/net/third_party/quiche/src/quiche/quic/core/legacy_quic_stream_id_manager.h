// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef QUICHE_QUIC_CORE_LEGACY_QUIC_STREAM_ID_MANAGER_H_
#define QUICHE_QUIC_CORE_LEGACY_QUIC_STREAM_ID_MANAGER_H_

#include "absl/container/flat_hash_set.h"
#include "quiche/quic/core/quic_stream_id_manager.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"

namespace quic {

namespace test {
class QuicSessionPeer;
}  // namespace test

class QuicSession;

// Manages Google QUIC stream IDs. This manager is responsible for two
// questions: 1) can next outgoing stream ID be allocated (if yes, what is the
// next outgoing stream ID) and 2) can a new incoming stream be opened.
class QUICHE_EXPORT LegacyQuicStreamIdManager {
 public:
  LegacyQuicStreamIdManager(Perspective perspective,
                            QuicTransportVersion transport_version,
                            size_t max_open_outgoing_streams,
                            size_t max_open_incoming_streams);

  ~LegacyQuicStreamIdManager();

  // Returns true if the next outgoing stream ID can be allocated.
  bool CanOpenNextOutgoingStream() const;

  // Returns true if a new incoming stream can be opened.
  bool CanOpenIncomingStream() const;

  // Returns false when increasing the largest created stream id to |id| would
  // violate the limit, so the connection should be closed.
  bool MaybeIncreaseLargestPeerStreamId(const QuicStreamId id);

  // Returns true if |id| is still available.
  bool IsAvailableStream(QuicStreamId id) const;

  // Returns the stream ID for a new outgoing stream, and increments the
  // underlying counter.
  QuicStreamId GetNextOutgoingStreamId();

  // Called when a new stream is open.
  void ActivateStream(bool is_incoming);

  // Called when a stream ID is closed.
  void OnStreamClosed(bool is_incoming);

  // Return true if |id| is peer initiated.
  bool IsIncomingStream(QuicStreamId id) const;

  size_t MaxAvailableStreams() const;

  void set_max_open_incoming_streams(size_t max_open_incoming_streams) {
    max_open_incoming_streams_ = max_open_incoming_streams;
  }

  void set_max_open_outgoing_streams(size_t max_open_outgoing_streams) {
    max_open_outgoing_streams_ = max_open_outgoing_streams;
  }

  void set_largest_peer_created_stream_id(
      QuicStreamId largest_peer_created_stream_id) {
    largest_peer_created_stream_id_ = largest_peer_created_stream_id;
  }

  size_t max_open_incoming_streams() const {
    return max_open_incoming_streams_;
  }

  size_t max_open_outgoing_streams() const {
    return max_open_outgoing_streams_;
  }

  QuicStreamId next_outgoing_stream_id() const {
    return next_outgoing_stream_id_;
  }

  QuicStreamId largest_peer_created_stream_id() const {
    return largest_peer_created_stream_id_;
  }

  size_t GetNumAvailableStreams() const;

  size_t num_open_incoming_streams() const {
    return num_open_incoming_streams_;
  }
  size_t num_open_outgoing_streams() const {
    return num_open_outgoing_streams_;
  }

 private:
  friend class test::QuicSessionPeer;

  const Perspective perspective_;
  const QuicTransportVersion transport_version_;

  // The maximum number of outgoing streams this connection can open.
  size_t max_open_outgoing_streams_;

  // The maximum number of incoming streams this connection will allow.
  size_t max_open_incoming_streams_;

  // The ID to use for the next outgoing stream.
  QuicStreamId next_outgoing_stream_id_;

  // Set of stream ids that are less than the largest stream id that has been
  // received, but are nonetheless available to be created.
  absl::flat_hash_set<QuicStreamId> available_streams_;

  QuicStreamId largest_peer_created_stream_id_;

  // A counter for peer initiated open streams.
  size_t num_open_incoming_streams_;

  // A counter for self initiated open streams.
  size_t num_open_outgoing_streams_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_LEGACY_QUIC_STREAM_ID_MANAGER_H_
