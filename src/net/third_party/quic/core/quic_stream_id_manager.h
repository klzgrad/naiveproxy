// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef NET_THIRD_PARTY_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_
#define NET_THIRD_PARTY_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_

#include "base/macros.h"
#include "net/third_party/quic/core/frames/quic_frame.h"
#include "net/third_party/quic/core/quic_types.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"

namespace quic {

namespace test {
class QuicStreamIdManagerPeer;
}  // namespace test

class QuicSession;

// Amount to increment a stream ID value to get the next stream ID in
// the stream ID space. Is 2 because even/odd stream ids are used to denote
// client- and server- initiated streams, respectively.
// TODO(fkastenholz): Need to update this for IETF stream id encoding when it is
// finalized
const QuicStreamId kV99StreamIdIncrement = 2;

// This constant controls the size of the window when deciding whether
// to generate a MAX STREAM ID frame or not. See the discussion of the
// window, below, for more details.
const int kMaxStreamIdWindowDivisor = 2;

// This class manages the stream ids for Version 99/IETF QUIC.
// TODO(fkastenholz): Expand to support bi- and uni-directional stream ids
// TODO(fkastenholz): Roll in pre-version-99 management
class QUIC_EXPORT_PRIVATE QuicStreamIdManager {
 public:
  QuicStreamIdManager(QuicSession* session,
                      size_t max_allowed_outgoing_streams,
                      size_t max_allowed_incoming_streams);

  // Generate a string suitable for sending to the log/etc to show current state
  // of the stream ID manager.
  QuicString DebugString() const {
    return QuicStrCat(
        " { max_allowed_outgoing_stream_id: ", max_allowed_outgoing_stream_id_,
        ", actual_max_allowed_incoming_stream_id_: ",
        actual_max_allowed_incoming_stream_id_,
        ", advertised_max_allowed_incoming_stream_id_: ",
        advertised_max_allowed_incoming_stream_id_,
        ", max_stream_id_window_: ", max_stream_id_window_,
        ", max_allowed_outgoing_streams_: ", max_allowed_outgoing_streams_,
        ", max_allowed_incoming_streams_: ", max_allowed_incoming_streams_,
        ", available_incoming_streams_: ", available_incoming_streams_,
        ", first_incoming_dynamic_stream_id_: ",
        first_incoming_dynamic_stream_id_,
        ", first_outgoing_dynamic_stream_id_: ",
        first_outgoing_dynamic_stream_id_, " }");
  }

  // Processes the MAX STREAM ID frame, invoked from
  // QuicSession::OnMaxStreamIdFrame. It has the same semantics as the
  // QuicFramerVisitorInterface, returning true if the framer should continue
  // processing the packet, false if not.
  bool OnMaxStreamIdFrame(const QuicMaxStreamIdFrame& frame);

  // Processes the STREAM ID BLOCKED frame, invoked from
  // QuicSession::OnStreamIdBlockedFrame. It has the same semantics as the
  // QuicFramerVisitorInterface, returning true if the framer should continue
  // processing the packet, false if not.
  bool OnStreamIdBlockedFrame(const QuicStreamIdBlockedFrame& frame);

  // Indicates whether the next outgoing stream ID can be allocated or not. The
  // test is whether it will exceed the maximum-stream-id or not.
  bool CanOpenNextOutgoingStream();

  // Generate and send a MAX_STREAM_ID frame.
  void SendMaxStreamIdFrame();

  // Invoked to deal with releasing a stream ID.
  void OnStreamClosed(QuicStreamId stream_id);

  // Returns the next outgoing stream id. If it fails (due to running into the
  // max_allowed_outgoing_stream_id limit) then it returns an invalid stream id.
  QuicStreamId GetNextOutgoingStreamId();

  // Check that an incoming stream id is valid -- is below the maximum allowed
  // stream ID. Note that this method uses the actual maximum, not the most
  // recently advertised maximum this helps preserve the Google-QUIC semantic
  // that we actually care about the number of open streams, not the maximum
  // stream ID.  Returns true if the stream ID is valid. If the stream ID fails
  // the test, will close the connection (per the protocol specification) and
  // return false. This method also maintains state with regard to the number of
  // streams that the peer can open (used for generating MAX_STREAM_ID frames).
  // This method should be called exactly once for each incoming stream
  // creation.
  bool OnIncomingStreamOpened(QuicStreamId stream_id);

  // Initialize the maximum allowed incoming stream id and number of streams.
  void SetMaxOpenIncomingStreams(size_t max_streams);

  // Initialize the maximum allowed outgoing stream id, number of streams, and
  // MAX_STREAM_ID advertisement window.
  void SetMaxOpenOutgoingStreams(size_t max_streams);

  // Register a new stream as a static stream. This is used so that the
  // advertised maximum stream ID can be calculated based on the start of the
  // dynamic stream space. This method will take any stream ID, one that either
  // this node or the peer will initiate.
  void RegisterStaticStream(QuicStreamId stream_id);

  size_t max_allowed_outgoing_streams() {
    return max_allowed_outgoing_streams_;
  }
  size_t max_allowed_incoming_streams() {
    return max_allowed_incoming_streams_;
  }
  QuicStreamId max_allowed_outgoing_stream_id() const {
    return max_allowed_outgoing_stream_id_;
  }
  QuicStreamId advertised_max_allowed_incoming_stream_id() const {
    return advertised_max_allowed_incoming_stream_id_;
  }
  QuicStreamId actual_max_allowed_incoming_stream_id() const {
    return actual_max_allowed_incoming_stream_id_;
  }
  QuicStreamId max_stream_id_window() const { return max_stream_id_window_; }

  QuicStreamId first_incoming_dynamic_stream_id() {
    return first_incoming_dynamic_stream_id_;
  }
  QuicStreamId first_outgoing_dynamic_stream_id() {
    return first_outgoing_dynamic_stream_id_;
  }
  size_t available_incoming_streams() { return available_incoming_streams_; }

  void set_max_allowed_incoming_streams(size_t stream_count) {
    max_allowed_incoming_streams_ = stream_count;
  }

 private:
  friend class test::QuicStreamIdManagerPeer;

  // Check whether the MAX_STREAM_ID window has opened up enough and, if so,
  // generate and send a MAX_STREAM_ID frame.
  void MaybeSendMaxStreamIdFrame();

  // Back reference to the session containing this Stream ID Manager.
  // needed to access various session methods, such as perspective()
  QuicSession* session_;

  // The maximum stream ID value that we can use. This is initialized based on,
  // first, the default number of open streams we can do, updated per the number
  // of streams we receive in the transport parameters, and then finally is
  // modified whenever a MAX_STREAM_ID frame is received from the peer.
  QuicStreamId max_allowed_outgoing_stream_id_;

  // Unlike for streams this node initiates, for incoming streams, there are two
  // maxima; the actual maximum which is the limit the peer must obey and the
  // maximum that was most recently advertised to the peer in a MAX_STREAM_ID
  // frame.
  //
  // The advertised maximum is never larger than the actual maximum. The actual
  // maximum increases whenever an incoming stream is closed. The advertised
  // maximum increases (to the actual maximum) whenever a MAX_STREAM_ID is sent.
  //
  // The peer is granted some leeway, incoming streams are accepted as long as
  // their stream id is not greater than the actual maximum.  The protocol
  // specifies that the advertised maximum is the limit. This implmentation uses
  // the actual maximum in order to support Google-QUIC semantics, where it's
  // the number of open streams, not their ID number, that is the real limit.
  QuicStreamId actual_max_allowed_incoming_stream_id_;
  QuicStreamId advertised_max_allowed_incoming_stream_id_;

  // max_stream_id_window_ is set to max_allowed_outgoing_streams_ / 2
  // (half of the number of streams that are allowed).  The local node
  // does not send a MAX_STREAM_ID frame to the peer until the local node
  // believes that the peer can open fewer than |max_stream_id_window_|
  // streams. When that is so, the local node sends a MAX_STREAM_ID every time
  // an inbound stream is closed.
  QuicStreamId max_stream_id_window_;

  // Maximum number of outgoing and incoming streams that are allowed to be
  // concurrently opened. Initialized as part of configuration.
  size_t max_allowed_outgoing_streams_;
  size_t max_allowed_incoming_streams_;

  // Keep track of the first dynamic stream id (which is the largest static
  // stream id plus one id).  For Google QUIC, static streams are not counted
  // against the stream count limit. When the number of static streams
  // increases, the maximum stream id has to increase by a corresponding amount.
  // These are used as floors from which the relevant maximum is
  // calculated. Keeping the "first dynamic" rather than the "last static" has
  // some implementation advantages.
  QuicStreamId first_incoming_dynamic_stream_id_;
  QuicStreamId first_outgoing_dynamic_stream_id_;

  // Number of streams that that this node believes that the
  // peer can open. It is initialized to the same value as
  // max_allowed_incoming_streams_. It is decremented every
  // time a new incoming stream is detected.  A MAX_STREAM_ID
  // is sent whenver a stream closes and this counter is less
  // than the window. When that happens, it is incremented by
  // the number of streams we make available (the actual max
  // stream ID - the most recently advertised one)
  size_t available_incoming_streams_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_CORE_QUIC_STREAM_ID_MANAGER_H_
