// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_control_frame_manager.h"

#include "net/quic/core/quic_constants.h"
#include "net/quic/platform/api/quic_bug_tracker.h"
#include "net/quic/platform/api/quic_map_util.h"

namespace net {

QuicControlFrameManager::QuicControlFrameManager() : least_unacked_(1) {}

QuicControlFrameManager::~QuicControlFrameManager() {
  while (!control_frames_.empty()) {
    DeleteFrame(&control_frames_.front());
    control_frames_.pop_front();
  }
}

void QuicControlFrameManager::OnControlFrameSent(const QuicFrame& frame) {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    QUIC_BUG
        << "Send or retransmit a control frame with invalid control frame id";
    return;
  }
  if (id == least_unacked_ + control_frames_.size()) {
    // This is a newly sent control frame. Save a copy of this frame.
    switch (frame.type) {
      case RST_STREAM_FRAME:
        control_frames_.emplace_back(
            QuicFrame(new QuicRstStreamFrame(*frame.rst_stream_frame)));
        return;
      case GOAWAY_FRAME:
        control_frames_.emplace_back(
            QuicFrame(new QuicGoAwayFrame(*frame.goaway_frame)));
        return;
      case WINDOW_UPDATE_FRAME:
        control_frames_.emplace_back(
            QuicFrame(new QuicWindowUpdateFrame(*frame.window_update_frame)));
        return;
      case BLOCKED_FRAME:
        control_frames_.emplace_back(
            QuicFrame(new QuicBlockedFrame(*frame.blocked_frame)));
        return;
      case PING_FRAME:
        control_frames_.emplace_back(
            QuicFrame(QuicPingFrame(frame.ping_frame.control_frame_id)));
        return;
      default:
        DCHECK(false);
        return;
    }
  }
  if (QuicContainsKey(pending_retransmissions_, id)) {
    // This is retransmitted control frame.
    pending_retransmissions_.erase(id);
    return;
  }
  QUIC_BUG << frame << " is neither a new or retransmitted control frame.";
}

void QuicControlFrameManager::OnControlFrameAcked(const QuicFrame& frame) {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    // Frame does not have a valid control frame ID, ignore it.
    return;
  }
  if (id < least_unacked_) {
    // This frame has already been acked.
    return;
  }
  if (id >= least_unacked_ + control_frames_.size()) {
    QUIC_BUG << "Try to ack unsent control frame";
    return;
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
}

void QuicControlFrameManager::OnControlFrameLost(const QuicFrame& frame) {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    // Frame does not have a valid control frame ID, ignore it.
    return;
  }
  if (id < least_unacked_ ||
      GetControlFrameId(control_frames_.at(id - least_unacked_)) ==
          kInvalidControlFrameId) {
    // This frame has already been acked.
    return;
  }
  if (id >= least_unacked_ + control_frames_.size()) {
    QUIC_BUG << "Try to mark unsent control frame as lost";
    return;
  }
  if (!QuicContainsKey(pending_retransmissions_, id)) {
    pending_retransmissions_[id] = true;
  }
}

bool QuicControlFrameManager::IsControlFrameOutstanding(
    const QuicFrame& frame) const {
  QuicControlFrameId id = GetControlFrameId(frame);
  if (id == kInvalidControlFrameId) {
    // Frame without a control frame ID should not be retransmitted.
    return false;
  }
  if (id >= least_unacked_ + control_frames_.size()) {
    QUIC_BUG << "Try to check retransmittability of an unsent frame.";
    return false;
  }
  return id >= least_unacked_ &&
         GetControlFrameId(control_frames_.at(id - least_unacked_)) !=
             kInvalidControlFrameId;
}

bool QuicControlFrameManager::HasPendingRetransmission() const {
  return !pending_retransmissions_.empty();
}

QuicFrame QuicControlFrameManager::NextPendingRetransmission() const {
  QUIC_BUG_IF(pending_retransmissions_.empty())
      << "Unexpected call to NextPendingRetransmission() with empty pending "
      << "retransmission list.";
  QuicControlFrameId id = pending_retransmissions_.begin()->first;
  return control_frames_.at(id - least_unacked_);
}

size_t QuicControlFrameManager::size() const {
  return control_frames_.size();
}

}  // namespace net
