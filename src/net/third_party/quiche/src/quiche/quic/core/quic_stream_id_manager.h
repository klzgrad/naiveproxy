// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_logging.h"

namespace quic {

namespace test {
class QuicSessionPeer;
class QuicStreamIdManagerPeer;
}  // namespace test

// This class manages the stream ids for IETF QUIC.
class QUICHE_EXPORT QuicStreamIdManager {
 public:
  class QUICHE_EXPORT DelegateInterface {
   public:
    virtual ~DelegateInterface() = default;

    // Returns true if a MAX_STREAMS frame can be sent.
    virtual bool CanSendMaxStreams() = 0;
    // Send a MAX_STREAMS frame.
    virtual void SendMaxStreams(QuicStreamCount stream_count,
                                bool unidirectional) = 0;
  };

  QuicStreamIdManager(DelegateInterface* delegate, bool unidirectional,
                      Perspective perspective, ParsedQuicVersion version,
                      QuicStreamCount max_allowed_outgoing_streams,
                      QuicStreamCount max_allowed_incoming_streams);

  ~QuicStreamIdManager();

  // Generate a string suitable for sending to the log/etc to show current state
  // of the stream ID manager.
  std::string DebugString() const {
    return absl::StrCat(
        " { unidirectional_: ", unidirectional_,
        ", perspective: ", perspective_,
        ", outgoing_max_streams_: ", outgoing_max_streams_,
        ", next_outgoing_stream_id_: ", next_outgoing_stream_id_,
        ", outgoing_stream_count_: ", outgoing_stream_count_,
        ", incoming_actual_max_streams_: ", incoming_actual_max_streams_,
        ", incoming_advertised_max_streams_: ",
        incoming_advertised_max_streams_,
        ", incoming_stream_count_: ", incoming_stream_count_,
        ", available_streams_.size(): ", available_streams_.size(),
        ", largest_peer_created_stream_id_: ", largest_peer_created_stream_id_,
        " }");
  }

  // Processes the STREAMS_BLOCKED frame. If error is encountered, populates
  // |error_details| and returns false.
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame,
                             std::string* error_details);

  // Returns whether the next outgoing stream ID can be allocated or not.
  bool CanOpenNextOutgoingStream() const;

  // Generate and send a MAX_STREAMS frame.
  void SendMaxStreamsFrame();

  // Invoked to deal with releasing a stream. Does nothing if the stream is
  // outgoing. If the stream is incoming, the number of streams that the peer
  // can open will be updated and a MAX_STREAMS frame, informing the peer of
  // the additional streams, may be sent.
  void OnStreamClosed(QuicStreamId stream_id);

  // Returns the next outgoing stream id. Applications must call
  // CanOpenNextOutgoingStream() first.
  QuicStreamId GetNextOutgoingStreamId();

  void SetMaxOpenIncomingStreams(QuicStreamCount max_open_streams);

  // Called on |max_open_streams| outgoing streams can be created because of 1)
  // config negotiated or 2) MAX_STREAMS received. Returns true if new
  // streams can be created.
  bool MaybeAllowNewOutgoingStreams(QuicStreamCount max_open_streams);

  // Checks if the incoming stream ID exceeds the MAX_STREAMS limit.  If the
  // limit is exceeded, populates |error_detials| and returns false.
  bool MaybeIncreaseLargestPeerStreamId(const QuicStreamId stream_id,
                                        std::string* error_details);

  // Returns true if |id| is still available.
  bool IsAvailableStream(QuicStreamId id) const;

  // Once called, the incoming max streams limit will never be increased.
  void StopIncreasingIncomingMaxStreams() {
    stop_increasing_incoming_max_streams_ = true;
  }

  QuicStreamCount incoming_initial_max_open_streams() const {
    return incoming_initial_max_open_streams_;
  }

  QuicStreamId next_outgoing_stream_id() const {
    return next_outgoing_stream_id_;
  }

  // Number of streams that the peer believes that it can still create.
  QuicStreamCount available_incoming_streams() const;

  QuicStreamId largest_peer_created_stream_id() const {
    return largest_peer_created_stream_id_;
  }

  QuicStreamCount outgoing_max_streams() const { return outgoing_max_streams_; }
  QuicStreamCount incoming_actual_max_streams() const {
    return incoming_actual_max_streams_;
  }
  QuicStreamCount incoming_advertised_max_streams() const {
    return incoming_advertised_max_streams_;
  }
  QuicStreamCount outgoing_stream_count() const {
    return outgoing_stream_count_;
  }

  // Check whether the MAX_STREAMS window has opened up enough and, if so,
  // generate and send a MAX_STREAMS frame.
  void MaybeSendMaxStreamsFrame();

 private:
  friend class test::QuicSessionPeer;
  friend class test::QuicStreamIdManagerPeer;

  // Get what should be the first incoming/outgoing stream ID that
  // this stream id manager will manage, taking into account directionality and
  // client/server perspective.
  QuicStreamId GetFirstOutgoingStreamId() const;
  QuicStreamId GetFirstIncomingStreamId() const;

  // Back reference to the session containing this Stream ID Manager.
  DelegateInterface* delegate_;

  // Whether this stream id manager is for unidrectional (true) or bidirectional
  // (false) streams.
  const bool unidirectional_;

  // Is this manager a client or a server.
  const Perspective perspective_;

  // QUIC version used for this manager.
  const ParsedQuicVersion version_;

  // The number of streams that this node can initiate.
  // This limit is first set when config is negotiated, but may be updated upon
  // receiving MAX_STREAMS frame.
  QuicStreamCount outgoing_max_streams_;

  // The ID to use for the next outgoing stream.
  QuicStreamId next_outgoing_stream_id_;

  // The number of outgoing streams that have ever been opened, including those
  // that have been closed. This number must never be larger than
  // outgoing_max_streams_.
  QuicStreamCount outgoing_stream_count_;

  // FOR INCOMING STREAMS

  // The actual maximum number of streams that can be opened by the peer.
  QuicStreamCount incoming_actual_max_streams_;
  // Max incoming stream number that has been advertised to the peer and is <=
  // incoming_actual_max_streams_. It is set to incoming_actual_max_streams_
  // when a MAX_STREAMS is sent.
  QuicStreamCount incoming_advertised_max_streams_;

  // Initial maximum on the number of open streams allowed.
  QuicStreamCount incoming_initial_max_open_streams_;

  // The number of streams that have been created, including open ones and
  // closed ones.
  QuicStreamCount incoming_stream_count_;

  // Set of stream ids that are less than the largest stream id that has been
  // received, but are nonetheless available to be created.
  absl::flat_hash_set<QuicStreamId> available_streams_;

  QuicStreamId largest_peer_created_stream_id_;

  // If true, then the stream limit will never be increased.
  bool stop_increasing_incoming_max_streams_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_
