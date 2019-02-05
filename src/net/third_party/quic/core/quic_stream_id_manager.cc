// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/third_party/quic/core/quic_stream_id_manager.h"

#include "net/third_party/quic/core/quic_connection.h"
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

QuicStreamIdManager::QuicStreamIdManager(QuicSession* session,
                                         size_t max_allowed_outgoing_streams,
                                         size_t max_allowed_incoming_streams)
    : session_(session),
      max_allowed_outgoing_stream_id_(0),
      actual_max_allowed_incoming_stream_id_(0),
      advertised_max_allowed_incoming_stream_id_(0),
      max_stream_id_window_(max_allowed_incoming_streams /
                            kMaxStreamIdWindowDivisor),
      max_allowed_incoming_streams_(max_allowed_incoming_streams),
      // Initialize to the peer's perspective.
      first_incoming_dynamic_stream_id_(
          QuicUtils::GetCryptoStreamId(
              session_->connection()->transport_version()) +
          (session_->perspective() == Perspective::IS_SERVER ? 2 : 1)),
      // Initialize to this node's perspective.
      first_outgoing_dynamic_stream_id_(
          QuicUtils::GetCryptoStreamId(
              session_->connection()->transport_version()) +
          (session_->perspective() == Perspective::IS_SERVER ? 1 : 2)) {
  available_incoming_streams_ = max_allowed_incoming_streams_;
  SetMaxOpenOutgoingStreams(max_allowed_outgoing_streams);
  SetMaxOpenIncomingStreams(max_allowed_incoming_streams);
}

bool QuicStreamIdManager::OnMaxStreamIdFrame(
    const QuicMaxStreamIdFrame& frame) {
  // Need to determine whether the stream id matches our client/server
  // perspective or not. If not, it's an error. If so, update appropriate
  // maxima.
  QUIC_CODE_COUNT_N(max_stream_id_received, 2, 2);
  // TODO(fkastenholz): this test needs to be broader to handle uni- and bi-
  // directional stream ids when that functionality is supported.
  if (session_->IsIncomingStream(frame.max_stream_id)) {
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
  QUIC_CODE_COUNT_N(stream_id_blocked_received, 2, 2);
  QuicStreamId id = frame.stream_id;
  if (!session_->IsIncomingStream(frame.stream_id)) {
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
  max_allowed_outgoing_stream_id_ = session_->next_outgoing_stream_id() +
                                    (max_streams - 1) * kV99StreamIdIncrement;
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
  if (!session_->IsIncomingStream(stream_id)) {
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
  QUIC_BUG_IF(session_->next_outgoing_stream_id() >
              max_allowed_outgoing_stream_id_)
      << "Attempt allocate a new outgoing stream ID would exceed the limit";
  QuicStreamId id = session_->next_outgoing_stream_id();
  session_->increment_next_outgoing_stream_id(kV99StreamIdIncrement);
  return id;
}

bool QuicStreamIdManager::CanOpenNextOutgoingStream() {
  DCHECK_EQ(QUIC_VERSION_99, session_->connection()->transport_version());
  if (session_->next_outgoing_stream_id() > max_allowed_outgoing_stream_id_) {
    // Next stream ID would exceed the limit, need to inform the peer.
    session_->SendStreamIdBlocked(max_allowed_outgoing_stream_id_);
    QUIC_CODE_COUNT(reached_outgoing_stream_id_limit);
    return false;
  }
  return true;
}

bool QuicStreamIdManager::OnIncomingStreamOpened(QuicStreamId stream_id) {
  // NOTE WELL: the protocol specifies that the peer must not send stream IDs
  // larger than what has been advertised in a MAX_STREAM_ID. The following test
  // will accept streams with IDs larger than the advertised maximum.  The limit
  // is the actual maximum, which increases as streams are closed. This
  // preserves the Google QUIC semantic that it is the number of streams (and
  // stream ids) that is important.
  if (stream_id <= actual_max_allowed_incoming_stream_id_) {
    available_incoming_streams_--;
    return true;
  }
  QUIC_CODE_COUNT(incoming_streamid_exceeds_limit);
  session_->connection()->CloseConnection(
      QUIC_INVALID_STREAM_ID,
      QuicStrCat(stream_id, " above ", actual_max_allowed_incoming_stream_id_),
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  return false;
}

void QuicStreamIdManager::RegisterStaticStream(QuicStreamId stream_id) {
  QuicStreamId first_dynamic_stream_id = stream_id + kV99StreamIdIncrement;

  if (session_->IsIncomingStream(first_dynamic_stream_id)) {
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

}  // namespace quic
