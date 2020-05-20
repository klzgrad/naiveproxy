// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_control_frame_manager.h"

#include <string>

#include "net/third_party/quiche/src/quic/core/quic_constants.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_map_util.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

namespace {

// The maximum number of buffered control frames which are waiting to be ACKed
// or sent for the first time.
const size_t kMaxNumControlFrames = 1000;

}  // namespace

#define ENDPOINT \
  (session_->perspective() == Perspective::IS_SERVER ? "Server: " : "Client: ")

QuicControlFrameManager::QuicControlFrameManager(QuicSession* session)
    : last_control_frame_id_(kInvalidControlFrameId),
      least_unacked_(1),
      least_unsent_(1),
      session_(session) {}

QuicControlFrameManager::~QuicControlFrameManager() {
  while (!control_frames_.empty()) {
    DeleteFrame(&control_frames_.front());
    control_frames_.pop_front();
  }
}

void QuicControlFrameManager::WriteOrBufferQuicFrame(QuicFrame frame) {
  const bool had_buffered_frames = HasBufferedFrames();
  control_frames_.emplace_back(frame);
  if (control_frames_.size() > kMaxNumControlFrames) {
    session_->connection()->CloseConnection(
        QUIC_TOO_MANY_BUFFERED_CONTROL_FRAMES,
        quiche::QuicheStrCat(
            "More than ", kMaxNumControlFrames,
            "buffered control frames, least_unacked: ", least_unacked_,
            ", least_unsent_: ", least_unsent_),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (had_buffered_frames) {
    return;
  }
  WriteBufferedFrames();
}

void QuicControlFrameManager::WriteOrBufferRstStream(
    QuicStreamId id,
    QuicRstStreamErrorCode error,
    QuicStreamOffset bytes_written) {
  QUIC_DVLOG(1) << "Writing RST_STREAM_FRAME";
  WriteOrBufferQuicFrame((QuicFrame(new QuicRstStreamFrame(
      ++last_control_frame_id_, id, error, bytes_written))));
}

void QuicControlFrameManager::WriteOrBufferGoAway(
    QuicErrorCode error,
    QuicStreamId last_good_stream_id,
    const std::string& reason) {
  QUIC_DVLOG(1) << "Writing GOAWAY_FRAME";
  WriteOrBufferQuicFrame(QuicFrame(new QuicGoAwayFrame(
      ++last_control_frame_id_, error, last_good_stream_id, reason)));
}

void QuicControlFrameManager::WriteOrBufferWindowUpdate(
    QuicStreamId id,
    QuicStreamOffset byte_offset) {
  QUIC_DVLOG(1) << "Writing WINDOW_UPDATE_FRAME";
  WriteOrBufferQuicFrame(QuicFrame(
      new QuicWindowUpdateFrame(++last_control_frame_id_, id, byte_offset)));
}

void QuicControlFrameManager::WriteOrBufferBlocked(QuicStreamId id) {
  QUIC_DVLOG(1) << "Writing BLOCKED_FRAME";
  WriteOrBufferQuicFrame(
      QuicFrame(new QuicBlockedFrame(++last_control_frame_id_, id)));
}

void QuicControlFrameManager::WriteOrBufferStreamsBlocked(QuicStreamCount count,
                                                          bool unidirectional) {
  QUIC_DVLOG(1) << "Writing STREAMS_BLOCKED Frame";
  QUIC_CODE_COUNT(quic_streams_blocked_transmits);
  WriteOrBufferQuicFrame(QuicFrame(QuicStreamsBlockedFrame(
      ++last_control_frame_id_, count, unidirectional)));
}

void QuicControlFrameManager::WriteOrBufferMaxStreams(QuicStreamCount count,
                                                      bool unidirectional) {
  QUIC_DVLOG(1) << "Writing MAX_STREAMS Frame";
  QUIC_CODE_COUNT(quic_max_streams_transmits);
  WriteOrBufferQuicFrame(QuicFrame(
      QuicMaxStreamsFrame(++last_control_frame_id_, count, unidirectional)));
}

void QuicControlFrameManager::WriteOrBufferStopSending(uint16_t code,
                                                       QuicStreamId stream_id) {
  QUIC_DVLOG(1) << "Writing STOP_SENDING_FRAME";
  WriteOrBufferQuicFrame(QuicFrame(
      new QuicStopSendingFrame(++last_control_frame_id_, stream_id, code)));
}

void QuicControlFrameManager::WriteOrBufferHandshakeDone() {
  QUIC_DVLOG(1) << "Writing HANDSHAKE_DONE";
  WriteOrBufferQuicFrame(
      QuicFrame(QuicHandshakeDoneFrame(++last_control_frame_id_)));
}

void QuicControlFrameManager::WritePing() {
  QUIC_DVLOG(1) << "Writing PING_FRAME";
  if (HasBufferedFrames()) {
    // Do not send ping if there is buffered frames.
    QUIC_LOG(WARNING)
        << "Try to send PING when there is buffered control frames.";
    return;
  }
  control_frames_.emplace_back(
      QuicFrame(QuicPingFrame(++last_control_frame_id_)));
  if (control_frames_.size() > kMaxNumControlFrames) {
    session_->connection()->CloseConnection(
        QUIC_TOO_MANY_BUFFERED_CONTROL_FRAMES,
        quiche::QuicheStrCat(
            "More than ", kMaxNumControlFrames,
            "buffered control frames, least_unacked: ", least_unacked_,
            ", least_unsent_: ", least_unsent_),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  WriteBufferedFrames();
}

void QuicControlFrameManager::OnControlFrameSent(const QuicFrame& frame) {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    QUIC_BUG
        << "Send or retransmit a control frame with invalid control frame id";
    return;
  }
  if (frame.type == WINDOW_UPDATE_FRAME) {
    QuicStreamId stream_id = frame.window_update_frame->stream_id;
    if (QuicContainsKey(window_update_frames_, stream_id) &&
        id > window_update_frames_[stream_id]) {
      // Consider the older window update of the same stream as acked.
      OnControlFrameIdAcked(window_update_frames_[stream_id]);
    }
    window_update_frames_[stream_id] = id;
  }
  if (QuicContainsKey(pending_retransmissions_, id)) {
    // This is retransmitted control frame.
    pending_retransmissions_.erase(id);
    return;
  }
  if (id > least_unsent_) {
    QUIC_BUG << "Try to send control frames out of order, id: " << id
             << " least_unsent: " << least_unsent_;
    session_->connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Try to send control frames out of order",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  ++least_unsent_;
}

bool QuicControlFrameManager::OnControlFrameAcked(const QuicFrame& frame) {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (!OnControlFrameIdAcked(id)) {
    return false;
  }
  if (frame.type == WINDOW_UPDATE_FRAME) {
    QuicStreamId stream_id = frame.window_update_frame->stream_id;
    if (QuicContainsKey(window_update_frames_, stream_id) &&
        window_update_frames_[stream_id] == id) {
      window_update_frames_.erase(stream_id);
    }
  }
  return true;
}

void QuicControlFrameManager::OnControlFrameLost(const QuicFrame& frame) {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    // Frame does not have a valid control frame ID, ignore it.
    return;
  }
  if (id >= least_unsent_) {
    QUIC_BUG << "Try to mark unsent control frame as lost";
    session_->connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Try to mark unsent control frame as lost",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  if (id < least_unacked_ ||
      GetControlFrameId(control_frames_.at(id - least_unacked_)) ==
          kInvalidControlFrameId) {
    // This frame has already been acked.
    return;
  }
  if (!QuicContainsKey(pending_retransmissions_, id)) {
    pending_retransmissions_[id] = true;
    QUIC_BUG_IF(pending_retransmissions_.size() > control_frames_.size())
        << "least_unacked_: " << least_unacked_
        << ", least_unsent_: " << least_unsent_;
  }
}

bool QuicControlFrameManager::IsControlFrameOutstanding(
    const QuicFrame& frame) const {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    // Frame without a control frame ID should not be retransmitted.
    return false;
  }
  // Consider this frame is outstanding if it does not get acked.
  return id < least_unacked_ + control_frames_.size() && id >= least_unacked_ &&
         GetControlFrameId(control_frames_.at(id - least_unacked_)) !=
             kInvalidControlFrameId;
}

bool QuicControlFrameManager::HasPendingRetransmission() const {
  return !pending_retransmissions_.empty();
}

bool QuicControlFrameManager::WillingToWrite() const {
  return HasPendingRetransmission() || HasBufferedFrames();
}

QuicFrame QuicControlFrameManager::NextPendingRetransmission() const {
  QUIC_BUG_IF(pending_retransmissions_.empty())
      << "Unexpected call to NextPendingRetransmission() with empty pending "
      << "retransmission list.";
  QuicControlFrameId id = pending_retransmissions_.begin()->first;
  return control_frames_.at(id - least_unacked_);
}

void QuicControlFrameManager::OnCanWrite() {
  if (HasPendingRetransmission()) {
    // Exit early to allow streams to write pending retransmissions if any.
    WritePendingRetransmission();
    return;
  }
  WriteBufferedFrames();
}

bool QuicControlFrameManager::RetransmitControlFrame(const QuicFrame& frame,
                                                     TransmissionType type) {
  DCHECK(type == PTO_RETRANSMISSION || type == RTO_RETRANSMISSION ||
         type == TLP_RETRANSMISSION || type == PROBING_RETRANSMISSION);
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    // Frame does not have a valid control frame ID, ignore it. Returns true
    // to allow writing following frames.
    return true;
  }
  if (id >= least_unsent_) {
    QUIC_BUG << "Try to retransmit unsent control frame";
    session_->connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Try to retransmit unsent control frame",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  if (id < least_unacked_ ||
      GetControlFrameId(control_frames_.at(id - least_unacked_)) ==
          kInvalidControlFrameId) {
    // This frame has already been acked.
    return true;
  }
  QuicFrame copy = CopyRetransmittableControlFrame(frame);
  QUIC_DVLOG(1) << "control frame manager is forced to retransmit frame: "
                << frame;
  if (session_->WriteControlFrame(copy, type)) {
    return true;
  }
  DeleteFrame(&copy);
  return false;
}

void QuicControlFrameManager::WriteBufferedFrames() {
  DCHECK(session_->connection()->connected())
      << ENDPOINT << "Try to write control frames when connection is closed.";
  while (HasBufferedFrames()) {
    if (!session_->write_with_transmission()) {
      session_->SetTransmissionType(NOT_RETRANSMISSION);
    }
    QuicFrame frame_to_send =
        control_frames_.at(least_unsent_ - least_unacked_);
    QuicFrame copy = CopyRetransmittableControlFrame(frame_to_send);
    if (!session_->WriteControlFrame(copy, NOT_RETRANSMISSION)) {
      // Connection is write blocked.
      DeleteFrame(&copy);
      break;
    }
    OnControlFrameSent(frame_to_send);
  }
}

void QuicControlFrameManager::WritePendingRetransmission() {
  while (HasPendingRetransmission()) {
    QuicFrame pending = NextPendingRetransmission();
    QuicFrame copy = CopyRetransmittableControlFrame(pending);
    if (!session_->WriteControlFrame(copy, LOSS_RETRANSMISSION)) {
      // Connection is write blocked.
      DeleteFrame(&copy);
      break;
    }
    OnControlFrameSent(pending);
  }
}

bool QuicControlFrameManager::OnControlFrameIdAcked(QuicControlFrameId id) {
  if (id == kInvalidControlFrameId) {
    // Frame does not have a valid control frame ID, ignore it.
    return false;
  }
  if (id >= least_unsent_) {
    QUIC_BUG << "Try to ack unsent control frame";
    session_->connection()->CloseConnection(
        QUIC_INTERNAL_ERROR, "Try to ack unsent control frame",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }
  if (id < least_unacked_ ||
      GetControlFrameId(control_frames_.at(id - least_unacked_)) ==
          kInvalidControlFrameId) {
    // This frame has already been acked.
    return false;
  }

  // Set control frame ID of acked frames to 0.
  SetControlFrameId(kInvalidControlFrameId,
                    &control_frames_.at(id - least_unacked_));
  // Remove acked control frames from pending retransmissions.
  pending_retransmissions_.erase(id);
  // Clean up control frames queue and increment least_unacked_.
  while (!control_frames_.empty() &&
         GetControlFrameId(control_frames_.front()) == kInvalidControlFrameId) {
    DeleteFrame(&control_frames_.front());
    control_frames_.pop_front();
    ++least_unacked_;
  }
  return true;
}

bool QuicControlFrameManager::HasBufferedFrames() const {
  return least_unsent_ < least_unacked_ + control_frames_.size();
}

}  // namespace quic
