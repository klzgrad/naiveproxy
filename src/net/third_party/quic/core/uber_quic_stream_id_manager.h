// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_CORE_UBER_QUIC_STREAM_ID_MANAGER_H_
#define NET_THIRD_PARTY_QUIC_CORE_UBER_QUIC_STREAM_ID_MANAGER_H_

#include "net/third_party/quic/core/quic_stream_id_manager.h"

namespace quic {

namespace test {
class QuicSessionPeer;
}  // namespace test

class QuicSession;

// This class comprises two QuicStreamIdManagers, which manage bidirectional and
// unidirectional stream IDs, respectively.
class QUIC_EXPORT_PRIVATE UberQuicStreamIdManager {
 public:
  UberQuicStreamIdManager(QuicSession* session,
                          size_t max_open_outgoing_streams,
                          size_t max_open_incoming_streams);

  // Called when a stream with |stream_id| is registered as a static stream.
  void RegisterStaticStream(QuicStreamId id);

  // Initialize the maximum allowed outgoing stream id, number of streams, and
  // MAX_STREAM_ID advertisement window.
  void SetMaxOpenOutgoingStreams(size_t max_streams);

  // Initialize the maximum allowed incoming stream id and number of streams.
  void SetMaxOpenIncomingStreams(size_t max_streams);

  // Returns true if next outgoing bidirectional stream ID can be allocated.
  bool CanOpenNextOutgoingBidirectionalStream();

  // Returns true if next outgoing unidirectional stream ID can be allocated.
  bool CanOpenNextOutgoingUnidirectionalStream();

  // Returns the next outgoing bidirectional stream id.
  QuicStreamId GetNextOutgoingBidirectionalStreamId();

  // Returns the next outgoing unidirectional stream id.
  QuicStreamId GetNextOutgoingUnidirectionalStreamId();

  // Returns true if allow to open the incoming |id|.
  bool MaybeIncreaseLargestPeerStreamId(QuicStreamId id);

  // Called when |id| is released.
  void OnStreamClosed(QuicStreamId id);

  // Called when a MAX_STREAM_ID frame is received.
  bool OnMaxStreamIdFrame(const QuicMaxStreamIdFrame& frame);

  // Called when a STREAM_ID_BLOCKED frame is received.
  bool OnStreamIdBlockedFrame(const QuicStreamIdBlockedFrame& frame);

  // Return true if |id| is peer initiated.
  bool IsIncomingStream(QuicStreamId id) const;

  // Returns true if |id| is still available.
  bool IsAvailableStream(QuicStreamId id) const;

  size_t GetMaxAllowdIncomingBidirectionalStreams() const;

  size_t GetMaxAllowdIncomingUnidirectionalStreams() const;

  void SetLargestPeerCreatedStreamId(
      QuicStreamId largest_peer_created_stream_id);

  QuicStreamId next_outgoing_bidirectional_stream_id() const;
  QuicStreamId next_outgoing_unidirectional_stream_id() const;

  QuicStreamId max_allowed_outgoing_bidirectional_stream_id() const;
  QuicStreamId max_allowed_outgoing_unidirectional_stream_id() const;

  size_t max_allowed_outgoing_bidirectional_streams() const;
  size_t max_allowed_outgoing_unidirectional_streams() const;

  QuicStreamId actual_max_allowed_incoming_bidirectional_stream_id() const;
  QuicStreamId actual_max_allowed_incoming_unidirectional_stream_id() const;

  QuicStreamId advertised_max_allowed_incoming_bidirectional_stream_id() const;
  QuicStreamId advertised_max_allowed_incoming_unidirectional_stream_id() const;

 private:
  friend class test::QuicSessionPeer;

  // Manages stream IDs of bidirectional streams.
  QuicStreamIdManager bidirectional_stream_id_manager_;

  // Manages stream IDs of unidirectional streams.
  QuicStreamIdManager unidirectional_stream_id_manager_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_UBER_QUIC_STREAM_ID_MANAGER_H_
