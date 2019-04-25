// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quic/core/quic_stream_id_manager.h"

#include "net/third_party/quic/core/quic_connection.h"
#include "net/third_party/quic/core/quic_constants.h"
#include "net/third_party/quic/core/quic_session.h"
#include "net/third_party/quic/core/quic_utils.h"
#include "net/third_party/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_logging.h"
#include "net/third_party/quic/platform/api/quic_str_cat.h"
#include "net/third_party/quic/platform/api/quic_string.h"

namespace quic {

#define ENDPOINT                                                   \
  (session_->perspective() == Perspective::IS_SERVER ? " Server: " \
                                                     : " Client: ")

QuicStreamIdManager::QuicStreamIdManager(
    QuicSession* session,
    QuicStreamId next_outgoing_stream_id,
    QuicStreamId largest_peer_created_stream_id,
    QuicStreamId first_incoming_dynamic_stream_id,
    size_t max_allowed_outgoing_streams,
    size_t max_allowed_incoming_streams)
    : session_(session),
      next_outgoing_stream_id_(next_outgoing_stream_id),
      largest_peer_created_stream_id_(largest_peer_created_stream_id),
      max_allowed_outgoing_stream_id_(0),
      actual_max_allowed_incoming_stream_id_(0),
      advertised_max_allowed_incoming_stream_id_(0),
      max_stream_id_window_(max_allowed_incoming_streams /
                            kMaxStreamIdWindowDivisor),
      max_allowed_incoming_streams_(max_allowed_incoming_streams),
      first_incoming_dynamic_stream_id_(first_incoming_dynamic_stream_id),
      first_outgoing_dynamic_stream_id_(next_outgoing_stream_id) {
  available_incoming_streams_ = max_allowed_incoming_streams_;
  SetMaxOpenOutgoingStreams(max_allowed_outgoing_streams);
  SetMaxOpenIncomingStreams(max_allowed_incoming_streams);
}

QuicStreamIdManager::~QuicStreamIdManager() {
  QUIC_LOG_IF(WARNING,
              session_->num_locally_closed_incoming_streams_highest_offset() >
                  max_allowed_incoming_streams_)
      << "Surprisingly high number of locally closed peer initiated streams"
         "still waiting for final byte offset: "
      << session_->num_locally_closed_incoming_streams_highest_offset();
  QUIC_LOG_IF(WARNING,
              session_->GetNumLocallyClosedOutgoingStreamsHighestOffset() >
                  max_allowed_outgoing_streams_)
      << "Surprisingly high number of locally closed self initiated streams"
         "still waiting for final byte offset: "
      << session_->GetNumLocallyClosedOutgoingStreamsHighestOffset();
}

bool QuicStreamIdManager::OnMaxStreamIdFrame(
    const QuicMaxStreamIdFrame& frame) {
  DCHECK_EQ(QuicUtils::IsBidirectionalStreamId(frame.max_stream_id),
            QuicUtils::IsBidirectionalStreamId(next_outgoing_stream_id_));
  // Need to determine whether the stream id matches our client/server
  // perspective or not. If not, it's an error. If so, update appropriate
  // maxima.
  QUIC_CODE_COUNT_N(max_stream_id_received, 2, 2);
  // TODO(fkastenholz): this test needs to be broader to handle uni- and bi-
  // directional stream ids when that functionality is supported.
  if (IsIncomingStream(frame.max_stream_id)) {
    // TODO(fkastenholz): This, and following, closeConnection may
    // need modification when proper support for IETF CONNECTION
    // CLOSE is done.
    QUIC_CODE_COUNT(max_stream_id_bad_direction);
    session_->connection()->CloseConnection(
        QUIC_MAX_STREAM_ID_ERROR,
        "Recevied max stream ID with wrong initiator bit setting",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  // If a MAX_STREAM_ID advertises a stream ID that is smaller than previously
  // advertised, it is to be ignored.
  if (frame.max_stream_id < max_allowed_outgoing_stream_id_) {
    QUIC_CODE_COUNT(max_stream_id_ignored);
    return true;
  }
  max_allowed_outgoing_stream_id_ = frame.max_stream_id;

  // Outgoing stream limit has increased, tell the applications
  session_->OnCanCreateNewOutgoingStream();

  return true;
}

bool QuicStreamIdManager::OnStreamIdBlockedFrame(
    const QuicStreamIdBlockedFrame& frame) {
  DCHECK_EQ(QuicUtils::IsBidirectionalStreamId(frame.stream_id),
            QuicUtils::IsBidirectionalStreamId(next_outgoing_stream_id_));
  QUIC_CODE_COUNT_N(stream_id_blocked_received, 2, 2);
  QuicStreamId id = frame.stream_id;
  if (!IsIncomingStream(frame.stream_id)) {
    // Client/server mismatch, close the connection
    // TODO(fkastenholz): revise when proper IETF Connection Close support is
    // done.
    QUIC_CODE_COUNT(stream_id_blocked_bad_direction);
    session_->connection()->CloseConnection(
        QUIC_STREAM_ID_BLOCKED_ERROR,
        "Invalid stream ID directionality specified",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  if (id > advertised_max_allowed_incoming_stream_id_) {
    // Peer thinks it can send more streams that we've told it.
    // This is a protocol error.
    // TODO(fkastenholz): revise when proper IETF Connection Close support is
    // done.
    QUIC_CODE_COUNT(stream_id_blocked_id_too_big);
    session_->connection()->CloseConnection(
        QUIC_STREAM_ID_BLOCKED_ERROR, "Invalid stream ID specified",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  if (id < actual_max_allowed_incoming_stream_id_) {
    // Peer thinks it's blocked on an ID that is less than our current
    // max. Inform the peer of the correct stream ID.
    SendMaxStreamIdFrame();
    return true;
  }
  // The peer's notion of the maximum ID is correct,
  // there is nothing to do.
  QUIC_CODE_COUNT(stream_id_blocked_id_correct);
  return true;
}

// TODO(fkastenholz): Many changes will be needed here:
//  -- Use IETF QUIC server/client-initiation sense
//  -- Support both BIDI and UNI streams.
//  -- can not change the max number of streams after config negotiation has
//     been done.
void QuicStreamIdManager::SetMaxOpenOutgoingStreams(size_t max_streams) {
  max_allowed_outgoing_streams_ = max_streams;
  max_allowed_outgoing_stream_id_ =
      next_outgoing_stream_id_ + (max_streams - 1) * kV99StreamIdIncrement;
}

// TODO(fkastenholz): Many changes will be needed here:
//  -- can not change the max number of streams after config negotiation has
//     been done.
//  -- Currently uses the Google Client/server-initiation sense, needs to
//     be IETF.
//  -- Support both BIDI and UNI streams.
//  -- Convert calculation of the maximum ID from Google-QUIC semantics to IETF
//     QUIC semantics.
void QuicStreamIdManager::SetMaxOpenIncomingStreams(size_t max_streams) {
  max_allowed_incoming_streams_ = max_streams;
  // The peer should always believe that it has the negotiated
  // number of stream ids available for use.
  available_incoming_streams_ = max_allowed_incoming_streams_;

  // the window is a fraction of the peer's notion of its stream-id space.
  max_stream_id_window_ =
      available_incoming_streams_ / kMaxStreamIdWindowDivisor;
  if (max_stream_id_window_ == 0) {
    max_stream_id_window_ = 1;
  }

  actual_max_allowed_incoming_stream_id_ =
      first_incoming_dynamic_stream_id_ +
      (max_allowed_incoming_streams_ - 1) * kV99StreamIdIncrement;
  // To start, we can assume advertised and actual are the same.
  advertised_max_allowed_incoming_stream_id_ =
      actual_max_allowed_incoming_stream_id_;
}

void QuicStreamIdManager::MaybeSendMaxStreamIdFrame() {
  if (available_incoming_streams_ > max_stream_id_window_) {
    // window too large, no advertisement
    return;
  }
  // Calculate the number of streams that the peer will believe
  // it has. The "/kV99StreamIdIncrement" converts from stream-id-
  // values to number-of-stream-ids.
  available_incoming_streams_ += (actual_max_allowed_incoming_stream_id_ -
                                  advertised_max_allowed_incoming_stream_id_) /
                                 kV99StreamIdIncrement;
  SendMaxStreamIdFrame();
}

void QuicStreamIdManager::SendMaxStreamIdFrame() {
  advertised_max_allowed_incoming_stream_id_ =
      actual_max_allowed_incoming_stream_id_;
  // And Advertise it.
  session_->SendMaxStreamId(advertised_max_allowed_incoming_stream_id_);
}

void QuicStreamIdManager::OnStreamClosed(QuicStreamId stream_id) {
  DCHECK_EQ(QuicUtils::IsBidirectionalStreamId(stream_id),
            QuicUtils::IsBidirectionalStreamId(next_outgoing_stream_id_));
  if (!IsIncomingStream(stream_id)) {
    // Nothing to do for outbound streams with respect to the
    // stream ID space management.
    return;
  }
  // If the stream is inbound, we can increase the stream ID limit and maybe
  // advertise the new limit to the peer.
  if (actual_max_allowed_incoming_stream_id_ >=
      (kMaxQuicStreamId - kV99StreamIdIncrement)) {
    // Reached the maximum stream id value that the implementation
    // supports. Nothing can be done here.
    return;
  }
  actual_max_allowed_incoming_stream_id_ += kV99StreamIdIncrement;
  MaybeSendMaxStreamIdFrame();
}

QuicStreamId QuicStreamIdManager::GetNextOutgoingStreamId() {
  QUIC_BUG_IF(next_outgoing_stream_id_ > max_allowed_outgoing_stream_id_)
      << "Attempt allocate a new outgoing stream ID would exceed the limit";
  QuicStreamId id = next_outgoing_stream_id_;
  next_outgoing_stream_id_ += kV99StreamIdIncrement;
  return id;
}

bool QuicStreamIdManager::CanOpenNextOutgoingStream() {
  DCHECK_EQ(QUIC_VERSION_99, session_->connection()->transport_version());
  if (next_outgoing_stream_id_ > max_allowed_outgoing_stream_id_) {
    // Next stream ID would exceed the limit, need to inform the peer.
    session_->SendStreamIdBlocked(max_allowed_outgoing_stream_id_);
    QUIC_CODE_COUNT(reached_outgoing_stream_id_limit);
    return false;
  }
  return true;
}

void QuicStreamIdManager::RegisterStaticStream(QuicStreamId stream_id) {
  DCHECK_EQ(QuicUtils::IsBidirectionalStreamId(stream_id),
            QuicUtils::IsBidirectionalStreamId(next_outgoing_stream_id_));
  QuicStreamId first_dynamic_stream_id = stream_id + kV99StreamIdIncrement;

  if (IsIncomingStream(first_dynamic_stream_id)) {
    // This code is predicated on static stream ids being allocated densely, in
    // order, and starting with the first stream allowed. QUIC_BUG if this is
    // not so.
    QUIC_BUG_IF(stream_id > first_incoming_dynamic_stream_id_)
        << "Error in incoming static stream allocation, expected to allocate "
        << first_incoming_dynamic_stream_id_ << " got " << stream_id;

    // This is a stream id for a stream that is started by the peer, deal with
    // the incoming stream ids. Increase the floor and adjust everything
    // accordingly.
    if (stream_id == first_incoming_dynamic_stream_id_) {
      actual_max_allowed_incoming_stream_id_ += kV99StreamIdIncrement;
      first_incoming_dynamic_stream_id_ = first_dynamic_stream_id;
    }
    return;
  }

  // This code is predicated on static stream ids being allocated densely, in
  // order, and starting with the first stream allowed. QUIC_BUG if this is
  // not so.
  QUIC_BUG_IF(stream_id > first_outgoing_dynamic_stream_id_)
      << "Error in outgoing static stream allocation, expected to allocate "
      << first_outgoing_dynamic_stream_id_ << " got " << stream_id;
  // This is a stream id for a stream that is started by this node; deal with
  // the outgoing stream ids. Increase the floor and adjust everything
  // accordingly.
  if (stream_id == first_outgoing_dynamic_stream_id_) {
    max_allowed_outgoing_stream_id_ += kV99StreamIdIncrement;
    first_outgoing_dynamic_stream_id_ = first_dynamic_stream_id;
  }
}

bool QuicStreamIdManager::MaybeIncreaseLargestPeerStreamId(
    const QuicStreamId stream_id) {
  DCHECK_EQ(QuicUtils::IsBidirectionalStreamId(stream_id),
            QuicUtils::IsBidirectionalStreamId(next_outgoing_stream_id_));
  available_streams_.erase(stream_id);

  if (largest_peer_created_stream_id_ !=
          QuicUtils::GetInvalidStreamId(
              session_->connection()->transport_version()) &&
      stream_id <= largest_peer_created_stream_id_) {
    return true;
  }

  if (stream_id > actual_max_allowed_incoming_stream_id_) {
    // Desired stream ID is larger than the limit, do not increase.
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Failed to create a new incoming stream with id:"
                    << stream_id << ".  Maximum allowed stream id is "
                    << actual_max_allowed_incoming_stream_id_ << ".";
    session_->connection()->CloseConnection(
        QUIC_INVALID_STREAM_ID,
        QuicStrCat("Stream id ", stream_id, " above ",
                   actual_max_allowed_incoming_stream_id_),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  available_incoming_streams_--;

  QuicStreamId id = largest_peer_created_stream_id_ + kV99StreamIdIncrement;
  if (largest_peer_created_stream_id_ ==
      QuicUtils::GetInvalidStreamId(
          session_->connection()->transport_version())) {
    // Adjust id based on perspective and whether stream_id is bidirectional or
    // unidirectional.
    if (QuicUtils::IsBidirectionalStreamId(stream_id)) {
      // This should only happen on client side because server bidirectional
      // stream ID manager's largest_peer_created_stream_id_ is initialized to
      // the crypto stream ID.
      DCHECK_EQ(Perspective::IS_CLIENT, session_->perspective());
      id = 1;
    } else {
      id = session_->perspective() == Perspective::IS_SERVER ? 2 : 3;
    }
  }
  for (; id < stream_id; id += kV99StreamIdIncrement) {
    available_streams_.insert(id);
  }
  largest_peer_created_stream_id_ = stream_id;
  return true;
}

bool QuicStreamIdManager::IsAvailableStream(QuicStreamId id) const {
  DCHECK_EQ(QuicUtils::IsBidirectionalStreamId(id),
            QuicUtils::IsBidirectionalStreamId(next_outgoing_stream_id_));
  if (!IsIncomingStream(id)) {
    // Stream IDs under next_ougoing_stream_id_ are either open or previously
    // open but now closed.
    return id >= next_outgoing_stream_id_;
  }
  // For peer created streams, we also need to consider available streams.
  return largest_peer_created_stream_id_ ==
             QuicUtils::GetInvalidStreamId(
                 session_->connection()->transport_version()) ||
         id > largest_peer_created_stream_id_ ||
         QuicContainsKey(available_streams_, id);
}

bool QuicStreamIdManager::IsIncomingStream(QuicStreamId id) const {
  DCHECK_EQ(QuicUtils::IsBidirectionalStreamId(id),
            QuicUtils::IsBidirectionalStreamId(next_outgoing_stream_id_));
  return id % kV99StreamIdIncrement !=
         next_outgoing_stream_id_ % kV99StreamIdIncrement;
}

}  // namespace quic
