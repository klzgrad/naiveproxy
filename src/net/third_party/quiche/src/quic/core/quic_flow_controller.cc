// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_flow_controller.h"

#include <cstdint>

#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

#define ENDPOINT \
  (perspective_ == Perspective::IS_SERVER ? "Server: " : "Client: ")

std::string QuicFlowController::LogLabel() {
  if (is_connection_flow_controller_) {
    return "connection";
  }
  return quiche::QuicheStrCat("stream ", id_);
}

QuicFlowController::QuicFlowController(
    QuicSession* session,
    QuicStreamId id,
    bool is_connection_flow_controller,
    QuicStreamOffset send_window_offset,
    QuicStreamOffset receive_window_offset,
    QuicByteCount receive_window_size_limit,
    bool should_auto_tune_receive_window,
    QuicFlowControllerInterface* session_flow_controller)
    : session_(session),
      connection_(session->connection()),
      id_(id),
      is_connection_flow_controller_(is_connection_flow_controller),
      perspective_(session->perspective()),
      bytes_sent_(0),
      send_window_offset_(send_window_offset),
      bytes_consumed_(0),
      highest_received_byte_offset_(0),
      receive_window_offset_(receive_window_offset),
      receive_window_size_(receive_window_offset),
      receive_window_size_limit_(receive_window_size_limit),
      auto_tune_receive_window_(should_auto_tune_receive_window),
      session_flow_controller_(session_flow_controller),
      last_blocked_send_window_offset_(0),
      prev_window_update_time_(QuicTime::Zero()) {
  DCHECK_LE(receive_window_size_, receive_window_size_limit_);
  DCHECK_EQ(
      is_connection_flow_controller_,
      QuicUtils::GetInvalidStreamId(session_->transport_version()) == id_);

  QUIC_DVLOG(1) << ENDPOINT << "Created flow controller for " << LogLabel()
                << ", setting initial receive window offset to: "
                << receive_window_offset_
                << ", max receive window to: " << receive_window_size_
                << ", max receive window limit to: "
                << receive_window_size_limit_
                << ", setting send window offset to: " << send_window_offset_;
}

void QuicFlowController::AddBytesConsumed(QuicByteCount bytes_consumed) {
  bytes_consumed_ += bytes_consumed;
  QUIC_DVLOG(1) << ENDPOINT << LogLabel() << " consumed " << bytes_consumed_
                << " bytes.";

  MaybeSendWindowUpdate();
}

bool QuicFlowController::UpdateHighestReceivedOffset(
    QuicStreamOffset new_offset) {
  // Only update if offset has increased.
  if (new_offset <= highest_received_byte_offset_) {
    return false;
  }

  QUIC_DVLOG(1) << ENDPOINT << LogLabel()
                << " highest byte offset increased from "
                << highest_received_byte_offset_ << " to " << new_offset;
  highest_received_byte_offset_ = new_offset;
  return true;
}

void QuicFlowController::AddBytesSent(QuicByteCount bytes_sent) {
  if (bytes_sent_ + bytes_sent > send_window_offset_) {
    QUIC_BUG << ENDPOINT << LogLabel() << " Trying to send an extra "
             << bytes_sent << " bytes, when bytes_sent = " << bytes_sent_
             << ", and send_window_offset_ = " << send_window_offset_;
    bytes_sent_ = send_window_offset_;

    // This is an error on our side, close the connection as soon as possible.
    connection_->CloseConnection(
        QUIC_FLOW_CONTROL_SENT_TOO_MUCH_DATA,
        quiche::QuicheStrCat(send_window_offset_ - (bytes_sent_ + bytes_sent),
                             "bytes over send window offset"),
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  bytes_sent_ += bytes_sent;
  QUIC_DVLOG(1) << ENDPOINT << LogLabel() << " sent " << bytes_sent_
                << " bytes.";
}

bool QuicFlowController::FlowControlViolation() {
  if (highest_received_byte_offset_ > receive_window_offset_) {
    QUIC_DLOG(INFO) << ENDPOINT << "Flow control violation on " << LogLabel()
                    << ", receive window offset: " << receive_window_offset_
                    << ", highest received byte offset: "
                    << highest_received_byte_offset_;
    return true;
  }
  return false;
}

void QuicFlowController::MaybeIncreaseMaxWindowSize() {
  // Core of receive window auto tuning.  This method should be called before a
  // WINDOW_UPDATE frame is sent.  Ideally, window updates should occur close to
  // once per RTT.  If a window update happens much faster than RTT, it implies
  // that the flow control window is imposing a bottleneck.  To prevent this,
  // this method will increase the receive window size (subject to a reasonable
  // upper bound).  For simplicity this algorithm is deliberately asymmetric, in
  // that it may increase window size but never decreases.

  // Keep track of timing between successive window updates.
  QuicTime now = connection_->clock()->ApproximateNow();
  QuicTime prev = prev_window_update_time_;
  prev_window_update_time_ = now;
  if (!prev.IsInitialized()) {
    QUIC_DVLOG(1) << ENDPOINT << "first window update for " << LogLabel();
    return;
  }

  if (!auto_tune_receive_window_) {
    return;
  }

  // Get outbound RTT.
  QuicTime::Delta rtt =
      connection_->sent_packet_manager().GetRttStats()->smoothed_rtt();
  if (rtt.IsZero()) {
    QUIC_DVLOG(1) << ENDPOINT << "rtt zero for " << LogLabel();
    return;
  }

  // Now we can compare timing of window updates with RTT.
  QuicTime::Delta since_last = now - prev;
  QuicTime::Delta two_rtt = 2 * rtt;

  if (since_last >= two_rtt) {
    // If interval between window updates is sufficiently large, there
    // is no need to increase receive_window_size_.
    return;
  }
  QuicByteCount old_window = receive_window_size_;
  IncreaseWindowSize();

  if (receive_window_size_ > old_window) {
    QUIC_DVLOG(1) << ENDPOINT << "New max window increase for " << LogLabel()
                  << " after " << since_last.ToMicroseconds()
                  << " us, and RTT is " << rtt.ToMicroseconds()
                  << "us. max wndw: " << receive_window_size_;
    if (session_flow_controller_ != nullptr) {
      session_flow_controller_->EnsureWindowAtLeast(
          kSessionFlowControlMultiplier * receive_window_size_);
    }
  } else {
    // TODO(ckrasic) - add a varz to track this (?).
    QUIC_LOG_FIRST_N(INFO, 1)
        << ENDPOINT << "Max window at limit for " << LogLabel() << " after "
        << since_last.ToMicroseconds() << " us, and RTT is "
        << rtt.ToMicroseconds() << "us. Limit size: " << receive_window_size_;
  }
}

void QuicFlowController::IncreaseWindowSize() {
  receive_window_size_ *= 2;
  receive_window_size_ =
      std::min(receive_window_size_, receive_window_size_limit_);
}

QuicByteCount QuicFlowController::WindowUpdateThreshold() {
  return receive_window_size_ / 2;
}

void QuicFlowController::MaybeSendWindowUpdate() {
  if (!session_->connection()->connected()) {
    return;
  }
  // Send WindowUpdate to increase receive window if
  // (receive window offset - consumed bytes) < (max window / 2).
  // This is behaviour copied from SPDY.
  DCHECK_LE(bytes_consumed_, receive_window_offset_);
  QuicStreamOffset available_window = receive_window_offset_ - bytes_consumed_;
  QuicByteCount threshold = WindowUpdateThreshold();

  if (!prev_window_update_time_.IsInitialized()) {
    // Treat the initial window as if it is a window update, so if 1/2 the
    // window is used in less than 2 RTTs, the window is increased.
    prev_window_update_time_ = connection_->clock()->ApproximateNow();
  }

  if (available_window >= threshold) {
    QUIC_DVLOG(1) << ENDPOINT << "Not sending WindowUpdate for " << LogLabel()
                  << ", available window: " << available_window
                  << " >= threshold: " << threshold;
    return;
  }

  MaybeIncreaseMaxWindowSize();
  UpdateReceiveWindowOffsetAndSendWindowUpdate(available_window);
}

void QuicFlowController::UpdateReceiveWindowOffsetAndSendWindowUpdate(
    QuicStreamOffset available_window) {
  // Update our receive window.
  receive_window_offset_ += (receive_window_size_ - available_window);

  QUIC_DVLOG(1) << ENDPOINT << "Sending WindowUpdate frame for " << LogLabel()
                << ", consumed bytes: " << bytes_consumed_
                << ", available window: " << available_window
                << ", and threshold: " << WindowUpdateThreshold()
                << ", and receive window size: " << receive_window_size_
                << ". New receive window offset is: " << receive_window_offset_;

  SendWindowUpdate();
}

bool QuicFlowController::ShouldSendBlocked() {
  if (SendWindowSize() != 0 ||
      last_blocked_send_window_offset_ >= send_window_offset_) {
    return false;
  }
  QUIC_DLOG(INFO) << ENDPOINT << LogLabel() << " is flow control blocked. "
                  << "Send window: " << SendWindowSize()
                  << ", bytes sent: " << bytes_sent_
                  << ", send limit: " << send_window_offset_;
  // The entire send_window has been consumed, we are now flow control
  // blocked.

  // Keep track of when we last sent a BLOCKED frame so that we only send one
  // at a given send offset.
  last_blocked_send_window_offset_ = send_window_offset_;
  return true;
}

bool QuicFlowController::UpdateSendWindowOffset(
    QuicStreamOffset new_send_window_offset) {
  // Only update if send window has increased.
  if (new_send_window_offset <= send_window_offset_) {
    return false;
  }

  QUIC_DVLOG(1) << ENDPOINT << "UpdateSendWindowOffset for " << LogLabel()
                << " with new offset " << new_send_window_offset
                << " current offset: " << send_window_offset_
                << " bytes_sent: " << bytes_sent_;

  // The flow is now unblocked but could have also been unblocked
  // before.  Return true iff this update caused a change from blocked
  // to unblocked.
  const bool was_previously_blocked = IsBlocked();
  send_window_offset_ = new_send_window_offset;
  return was_previously_blocked;
}

void QuicFlowController::EnsureWindowAtLeast(QuicByteCount window_size) {
  if (receive_window_size_limit_ >= window_size) {
    return;
  }

  QuicStreamOffset available_window = receive_window_offset_ - bytes_consumed_;
  IncreaseWindowSize();
  UpdateReceiveWindowOffsetAndSendWindowUpdate(available_window);
}

bool QuicFlowController::IsBlocked() const {
  return SendWindowSize() == 0;
}

uint64_t QuicFlowController::SendWindowSize() const {
  if (bytes_sent_ > send_window_offset_) {
    return 0;
  }
  return send_window_offset_ - bytes_sent_;
}

void QuicFlowController::UpdateReceiveWindowSize(QuicStreamOffset size) {
  DCHECK_LE(size, receive_window_size_limit_);
  QUIC_DVLOG(1) << ENDPOINT << "UpdateReceiveWindowSize for " << LogLabel()
                << ": " << size;
  if (receive_window_size_ != receive_window_offset_) {
    QUIC_BUG << "receive_window_size_:" << receive_window_size_
             << " != receive_window_offset:" << receive_window_offset_;
    return;
  }
  receive_window_size_ = size;
  receive_window_offset_ = size;
}

void QuicFlowController::SendWindowUpdate() {
  QuicStreamId id = id_;
  if (is_connection_flow_controller_) {
    id = QuicUtils::GetInvalidStreamId(connection_->transport_version());
  }
  session_->SendWindowUpdate(id, receive_window_offset_);
}

}  // namespace quic
