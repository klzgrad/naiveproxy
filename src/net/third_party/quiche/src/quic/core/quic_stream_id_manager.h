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
  class DelegateInterface {
   public:
    virtual ~DelegateInterface() = default;

    // Called when new outgoing streams are available to be opened. This occurs
    // when an extant, open, stream is moved to draining or closed.
    // |unidirectional| indicates whether unidirectional or bidirectional
    // streams are now available. If both become available at the same time then
    // there will be two calls to this method, one with unidirectional==true,
    // the other with it ==false.
    virtual void OnCanCreateNewOutgoingStream(bool unidirectional) = 0;

    // Closes the connection when an error is encountered.
    virtual void OnError(QuicErrorCode error_code,
                         std::string error_details) = 0;

    // Send a MAX_STREAMS frame.
    virtual void SendMaxStreams(QuicStreamCount stream_count,
                                bool unidirectional) = 0;

    // Send a STREAMS_BLOCKED frame.
    virtual void SendStreamsBlocked(QuicStreamCount stream_count,
                                    bool unidirectional) = 0;
  };

  QuicStreamIdManager(DelegateInterface* delegate,
                      bool unidirectional,
                      Perspective perspective,
                      QuicTransportVersion transport_version,
                      QuicStreamCount num_expected_static_streams,
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
        ", using_default_max_streams_: ", using_default_max_streams_,
        ", incoming_actual_max_streams_: ", incoming_actual_max_streams_,
        ", incoming_advertised_max_streams_: ",
        incoming_advertised_max_streams_,
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

  void SetMaxOpenIncomingStreams(size_t max_open_streams);

  // Sets the maximum number of outgoing streams to max_open_streams.
  // Used when configuration has been done and we have an initial
  // maximum stream count from the peer. Note that if the stream count is such
  // that it would result in stream ID values that are greater than the
  // implementation limit, it pegs the count at the implementation limit.
  bool SetMaxOpenOutgoingStreams(size_t max_open_streams);

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

  // Called when session has been configured. Causes the Stream ID manager to
  // send out any pending MAX_STREAMS and STREAMS_BLOCKED frames.
  void OnConfigNegotiated();

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
  DelegateInterface* delegate_;

  // Whether this stream id manager is for unidrectional (true) or bidirectional
  // (false) streams.
  const bool unidirectional_;

  // Is this manager a client or a server.
  const Perspective perspective_;

  // Transport version used for this manager.
  const QuicTransportVersion transport_version_;

  // Number of expected static streams.
  const QuicStreamCount num_expected_static_streams_;

  // True if the config has been negotiated_;
  bool is_config_negotiated_;

  // This is the number of streams that this node can initiate.
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

  // Set to true while the default (from the constructor) outgoing stream limit
  // is in use. It is set to false when either a MAX STREAMS frame is received
  // or the transport negotiation completes and sets the stream limit (this is
  // equivalent to a MAX_STREAMS frame).
  // Necessary because outgoing_max_streams_ is a "best guess"
  // until we receive an authoritative value from the peer.
  // outgoing_max_streams_ is initialized in the constructor
  // to some hard-coded value, which may or may not be consistent
  // with what the peer wants.
  bool using_default_max_streams_;

  // FOR INCOMING STREAMS

  // The maximum number of streams that can be opened by the peer.
  QuicStreamCount incoming_actual_max_streams_;
  QuicStreamCount incoming_advertised_max_streams_;

  // Initial maximum on the number of open streams allowed.
  QuicStreamCount incoming_initial_max_open_streams_;

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

  // MAX_STREAMS and STREAMS_BLOCKED frames are not sent before the session has
  // been configured. Instead, the relevant information is stored in
  // |pending_max_streams_| and |pending_streams_blocked_| and sent when
  // OnConfigNegotiated() is invoked.
  bool pending_max_streams_;
  QuicStreamId pending_streams_blocked_;
};
}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_
