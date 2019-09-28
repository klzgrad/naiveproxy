// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_

#include "net/third_party/quiche/src/quic/core/frames/quic_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_str_cat.h"

namespace quic {

namespace test {
class QuicSessionPeer;
class QuicStreamIdManagerPeer;
}  // namespace test

class QuicSession;

// Amount to increment a stream ID value to get the next stream ID in
// the stream ID space.
const QuicStreamId kV99StreamIdIncrement = 4;

// This constant controls the size of the window when deciding whether
// to generate a MAX_STREAMS frame or not. See the discussion of the
// window, below, for more details.
const int kMaxStreamsWindowDivisor = 2;

// This class manages the stream ids for Version 99/IETF QUIC.
class QUIC_EXPORT_PRIVATE QuicStreamIdManager {
 public:
  QuicStreamIdManager(QuicSession* session,
                      bool unidirectional,
                      QuicStreamCount max_allowed_outgoing_streams,
                      QuicStreamCount max_allowed_incoming_streams);

  ~QuicStreamIdManager();

  // Generate a string suitable for sending to the log/etc to show current state
  // of the stream ID manager.
  std::string DebugString() const {
    return QuicStrCat(
        " { unidirectional_: ", unidirectional_,
        ", perspective: ", perspective(),
        ", outgoing_max_streams_: ", outgoing_max_streams_,
        ", next_outgoing_stream_id_: ", next_outgoing_stream_id_,
        ", outgoing_stream_count_: ", outgoing_stream_count_,
        ", outgoing_static_stream_count_: ", outgoing_static_stream_count_,
        ", using_default_max_streams_: ", using_default_max_streams_,
        ", incoming_actual_max_streams_: ", incoming_actual_max_streams_,
        ", incoming_advertised_max_streams_: ",
        incoming_advertised_max_streams_,
        ", incoming_static_stream_count_: ", incoming_static_stream_count_,
        ", incoming_stream_count_: ", incoming_stream_count_,
        ", available_streams_.size(): ", available_streams_.size(),
        ", largest_peer_created_stream_id_: ", largest_peer_created_stream_id_,
        ", max_streams_window_: ", max_streams_window_, " }");
  }

  // Processes the MAX_STREAMS frame, invoked from
  // QuicSession::OnMaxStreamsFrame. It has the same semantics as the
  // QuicFramerVisitorInterface, returning true if the framer should continue
  // processing the packet, false if not.
  bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame);

  // Processes the STREAMS_BLOCKED frame, invoked from
  // QuicSession::OnStreamsBlockedFrame. It has the same semantics as the
  // QuicFramerVisitorInterface, returning true if the framer should continue
  // processing the packet, false if not.
  bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame);

  // Indicates whether the next outgoing stream ID can be allocated or not.
  bool CanOpenNextOutgoingStream();

  // Generate and send a MAX_STREAMS frame.
  void SendMaxStreamsFrame();

  // Invoked to deal with releasing a stream. Does nothing if the stream is
  // outgoing. If the stream is incoming, the number of streams that the peer
  // can open will be updated and a MAX_STREAMS frame, informing the peer of
  // the additional streams, may be sent.
  void OnStreamClosed(QuicStreamId stream_id);

  // Returns the next outgoing stream id. Applications must call
  // CanOpenNextOutgoingStream() first.  A QUIC_BUG is logged if this method
  // allocates a stream ID past the peer specified limit.
  QuicStreamId GetNextOutgoingStreamId();

  // Set the outgoing stream limits to be |max_open_streams| plus the number
  // of static streams that have been opened. For outgoing and incoming,
  // respectively.
  // SetMaxOpenOutgoingStreams will QUIC_BUG if it is called after
  // a MAX_STREAMS frame has been received.
  // TODO(fkastenholz): When static streams disappear, these should be removed.
  void SetMaxOpenOutgoingStreams(size_t max_open_streams);
  void SetMaxOpenIncomingStreams(size_t max_open_streams);

  // Adjust the outgoing stream limit - max_open_streams is the limit, not
  // including static streams. Does not QUIC_BUG if it is called _after_
  // receiving a MAX_STREAMS.
  void AdjustMaxOpenOutgoingStreams(size_t max_open_streams);

  // Sets the maximum number of outgoing streams to max_open_streams.
  // Used when configuration has been done and we have an initial
  // maximum stream count from the peer. Note that if the stream count is such
  // that it would result in stream ID values that are greater than the
  // implementation limit, it pegs the count at the implementation limit.
  bool ConfigureMaxOpenOutgoingStreams(size_t max_open_streams);

  // Register a new stream as a static stream. This is used so that the
  // advertised MAX STREAMS can be calculated based on the start of the
  // dynamic stream space. This method will take any stream ID, one that either
  // this node or the peer will initiate.
  // If |stream_already_counted| is true, the stream is already counted as an
  // open stream else where, so no need to count it again.
  // Returns false if this fails because the new static stream would cause the
  // stream limit to be exceeded.
  bool RegisterStaticStream(QuicStreamId stream_id,
                            bool stream_already_counted);

  // Checks if the incoming stream ID exceeds the MAX_STREAMS limit.  If the
  // limit is exceeded, closes the connection and returns false.  Uses the
  // actual maximium, not the most recently advertised value, in order to
  // enforce the Google-QUIC number of open streams behavior.
  // This method should be called exactly once for each incoming stream
  // creation.
  bool MaybeIncreaseLargestPeerStreamId(const QuicStreamId stream_id);

  // Returns true if |id| is still available.
  bool IsAvailableStream(QuicStreamId id) const;

  // Return true if given stream is peer initiated.
  bool IsIncomingStream(QuicStreamId id) const;

  size_t outgoing_static_stream_count() const {
    return outgoing_static_stream_count_;
  }

  size_t incoming_initial_max_open_streams() const {
    return incoming_initial_max_open_streams_;
  }

  QuicStreamCount max_streams_window() const { return max_streams_window_; }

  QuicStreamId next_outgoing_stream_id() const {
    return next_outgoing_stream_id_;
  }

  // Number of streams that the peer believes that it can still create.
  size_t available_incoming_streams();

  void set_largest_peer_created_stream_id(
      QuicStreamId largest_peer_created_stream_id) {
    largest_peer_created_stream_id_ = largest_peer_created_stream_id;
  }

  // These are the limits for outgoing and incoming streams,
  // respectively. For incoming there are two limits, what has
  // been advertised to the peer and what is actually available.
  // The advertised incoming amount should never be more than the actual
  // incoming one.
  QuicStreamCount outgoing_max_streams() const { return outgoing_max_streams_; }
  QuicStreamCount incoming_actual_max_streams() const {
    return incoming_actual_max_streams_;
  }
  QuicStreamCount incoming_advertised_max_streams() const {
    return incoming_advertised_max_streams_;
  }
  // Number of streams that have been opened (including those that have been
  // opened and then closed. Must never exceed outgoing_max_streams
  QuicStreamCount outgoing_stream_count() { return outgoing_stream_count_; }

  // Perspective (CLIENT/SERVER) of this node and the peer, respectively.
  Perspective perspective() const;
  Perspective peer_perspective() const;

  QuicTransportVersion transport_version() const;

 private:
  friend class test::QuicSessionPeer;
  friend class test::QuicStreamIdManagerPeer;

  // Check whether the MAX_STREAMS window has opened up enough and, if so,
  // generate and send a MAX_STREAMS frame.
  void MaybeSendMaxStreamsFrame();

  // Get what should be the first incoming/outgoing stream ID that
  // this stream id manager will manage, taking into account directionality and
  // client/server perspective.
  QuicStreamId GetFirstOutgoingStreamId() const;
  QuicStreamId GetFirstIncomingStreamId() const;

  void CalculateIncomingMaxStreamsWindow();

  // Back reference to the session containing this Stream ID Manager.
  // needed to access various session methods, such as perspective()
  QuicSession* session_;

  // Whether this stream id manager is for unidrectional (true) or bidirectional
  // (false) streams.
  bool unidirectional_;

  // This is the number of streams that this node can initiate.
  // This limit applies to both static and dynamic streams - the total
  // of the two can not exceed this count.
  // This limit is:
  //   - Initiated to a value specified in the constructor
  //   - May be updated when the config is received.
  //   - Is updated whenever a MAX STREAMS frame is received.
  QuicStreamCount outgoing_max_streams_;

  // The ID to use for the next outgoing stream.
  QuicStreamId next_outgoing_stream_id_;

  // The number of outgoing streams that have ever been opened, including those
  // that have been closed. This number must never be larger than
  // outgoing_max_streams_.
  QuicStreamCount outgoing_stream_count_;

  // Number of outgoing static streams created.
  // TODO(fkastenholz): Remove when static streams no longer supported for IETF
  // QUIC.
  QuicStreamCount outgoing_static_stream_count_;

  // Set to true while the default (from the constructor) outgoing stream limit
  // is in use. It is set to false when either a MAX STREAMS frame is received
  // or the transport negotiation completes and sets the stream limit (this is
  // equivalent to a MAX_STREAMS frame).
  // Necessary because outgoing_max_streams_ is a "best guess"
  // until we receive an authoritative value from the peer.
  // outgoing_max_streams_ is initialized in the constructor
  // to some hard-coded value, which may or may not be consistent
  // with what the peer wants. Furthermore, as we create outgoing
  // static streams, the cap raises as static streams get inserted
  // "beneath" the dynamic streams because, prior to receiving
  // a MAX_STREAMS, the values setting the limit are interpreted
  // as "number of request/responses" that can be created. Once
  // a MAX_STREAMS is received, it becomes a hard limit.
  bool using_default_max_streams_;

  // FOR INCOMING STREAMS

  // The maximum number of streams that can be opened by the peer.
  QuicStreamCount incoming_actual_max_streams_;
  QuicStreamCount incoming_advertised_max_streams_;

  // Initial maximum on the number of open streams allowed.
  QuicStreamCount incoming_initial_max_open_streams_;

  // Number of outgoing static streams created.
  // TODO(fkastenholz): Remove when static streams no longer supported for IETF
  // QUIC.
  QuicStreamCount incoming_static_stream_count_;

  // This is the number of streams that have been created -- some are still
  // open, the others have been closed. It is the number that is compared
  // against MAX_STREAMS when deciding whether to accept a new stream or not.
  QuicStreamCount incoming_stream_count_;

  // Set of stream ids that are less than the largest stream id that has been
  // received, but are nonetheless available to be created.
  QuicUnorderedSet<QuicStreamId> available_streams_;

  QuicStreamId largest_peer_created_stream_id_;

  // When incoming streams close the local node sends MAX_STREAMS frames. It
  // does so only when the peer can open fewer than |max_stream_id_window_|
  // streams. That is, when |incoming_actual_max_streams_| -
  // |incoming_advertised_max_streams_| is less than the window.
  // max_streams_window_ is set to 1/2 of the initial number of incoming streams
  // that are allowed (as set in the constructor).
  QuicStreamId max_streams_window_;
};
}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_
