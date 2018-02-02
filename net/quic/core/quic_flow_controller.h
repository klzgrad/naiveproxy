// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_FLOW_CONTROLLER_H_
#define NET_QUIC_CORE_QUIC_FLOW_CONTROLLER_H_

#include "base/macros.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {

namespace test {
class QuicFlowControllerPeer;
}  // namespace test

class QuicConnection;

const QuicStreamId kConnectionLevelId = 0;

// How much larger the session flow control window needs to be relative to any
// stream's flow control window.
const float kSessionFlowControlMultiplier = 1.5;

class QUIC_EXPORT_PRIVATE QuicFlowControllerInterface {
 public:
  virtual ~QuicFlowControllerInterface() {}

  // Ensures the flow control window is at least |window_size| and send out an
  // update frame if it is increased.
  virtual void EnsureWindowAtLeast(QuicByteCount window_size) = 0;
};

// QuicFlowController allows a QUIC stream or connection to perform flow
// control. The stream/connection owns a QuicFlowController which keeps track of
// bytes sent/received, can tell the owner if it is flow control blocked, and
// can send WINDOW_UPDATE or BLOCKED frames when needed.
class QUIC_EXPORT_PRIVATE QuicFlowController
    : public QuicFlowControllerInterface {
 public:
  QuicFlowController(QuicConnection* connection,
                     QuicStreamId id,
                     Perspective perspective,
                     QuicStreamOffset send_window_size,
                     QuicStreamOffset receive_window_size,
                     bool should_auto_tune_receive_window,
                     QuicFlowControllerInterface* session_flow_controller);

  ~QuicFlowController() override {}

  // Called when we see a new highest received byte offset from the peer, either
  // via a data frame or a RST.
  // Returns true if this call changes highest_received_byte_offset_, and false
  // in the case where |new_offset| is <= highest_received_byte_offset_.
  bool UpdateHighestReceivedOffset(QuicStreamOffset new_offset);

  // Called when bytes received from the peer are consumed locally. This may
  // trigger the sending of a WINDOW_UPDATE frame using |connection|.
  void AddBytesConsumed(QuicByteCount bytes_consumed);

  // Called when bytes are sent to the peer.
  void AddBytesSent(QuicByteCount bytes_sent);

  // Increases |send_window_offset_| if |new_send_window_offset| is
  // greater than the current value.  Returns true if this increase
  // also causes us to change from a blocked state to unblocked.  In
  // all other cases, returns false.
  bool UpdateSendWindowOffset(QuicStreamOffset new_send_window_offset);

  // QuicFlowControllerInterface.
  void EnsureWindowAtLeast(QuicByteCount window_size) override;

  // Returns the current available send window.
  QuicByteCount SendWindowSize() const;

  // Send a BLOCKED frame if appropriate.
  void MaybeSendBlocked();

  // Returns true if flow control send limits have been reached.
  bool IsBlocked() const;

  // Returns true if flow control receive limits have been violated by the peer.
  bool FlowControlViolation();

  // Inform the peer of new receive window.
  void SendWindowUpdate();

  QuicByteCount bytes_consumed() const { return bytes_consumed_; }

  QuicStreamOffset highest_received_byte_offset() const {
    return highest_received_byte_offset_;
  }

  void set_receive_window_size_limit(QuicByteCount receive_window_size_limit) {
    DCHECK_GE(receive_window_size_limit, receive_window_size_limit_);
    receive_window_size_limit_ = receive_window_size_limit;
  }

  // Should only be called before any data is received.
  void UpdateReceiveWindowSize(QuicStreamOffset size);

  bool auto_tune_receive_window() { return auto_tune_receive_window_; }

 private:
  friend class test::QuicFlowControllerPeer;

  // Send a WINDOW_UPDATE frame if appropriate.
  void MaybeSendWindowUpdate();

  // Auto-tune the max receive window size.
  void MaybeIncreaseMaxWindowSize();

  // Updates the current offset and sends a window update frame.
  void UpdateReceiveWindowOffsetAndSendWindowUpdate(
      QuicStreamOffset available_window);

  // Double the window size as long as we haven't hit the max window size.
  void IncreaseWindowSize();

  // The parent connection, used to send connection close on flow control
  // violation, and WINDOW_UPDATE and BLOCKED frames when appropriate.
  // Not owned.
  QuicConnection* connection_;

  // ID of stream this flow controller belongs to. This can be 0 if this is a
  // connection level flow controller.
  QuicStreamId id_;

  // Tracks if this is owned by a server or a client.
  Perspective perspective_;

  // Tracks number of bytes sent to the peer.
  QuicByteCount bytes_sent_;

  // The absolute offset in the outgoing byte stream. If this offset is reached
  // then we become flow control blocked until we receive a WINDOW_UPDATE.
  QuicStreamOffset send_window_offset_;

  // Overview of receive flow controller.
  //
  // 0=...===1=======2-------3 ...... FIN
  //         |<--- <= 4  --->|
  //

  // 1) bytes_consumed_ - moves forward when data is read out of the
  //    stream.
  //
  // 2) highest_received_byte_offset_ - moves when data is received
  //    from the peer.
  //
  // 3) receive_window_offset_ - moves when WINDOW_UPDATE is sent.
  //
  // 4) receive_window_size_ - maximum allowed unread data (3 - 1).
  //    This value may be increased by auto-tuning.
  //
  // 5) receive_window_size_limit_ - limit on receive_window_size_;
  //    auto-tuning will not increase window size beyond this limit.

  // Track number of bytes received from the peer, which have been consumed
  // locally.
  QuicByteCount bytes_consumed_;

  // The highest byte offset we have seen from the peer. This could be the
  // highest offset in a data frame, or a final value in a RST.
  QuicStreamOffset highest_received_byte_offset_;

  // The absolute offset in the incoming byte stream. The peer should never send
  // us bytes which are beyond this offset.
  QuicStreamOffset receive_window_offset_;

  // Largest size the receive window can grow to.
  QuicByteCount receive_window_size_;

  // Upper limit on receive_window_size_;
  QuicByteCount receive_window_size_limit_;

  // Used to dynamically enable receive window auto-tuning.
  bool auto_tune_receive_window_;

  // The session's flow controller.  null if this is stream id 0.
  // Not owned.
  QuicFlowControllerInterface* session_flow_controller_;

  // Send window update when receive window size drops below this.
  QuicByteCount WindowUpdateThreshold();

  // Keep track of the last time we sent a BLOCKED frame. We should only send
  // another when the number of bytes we have sent has changed.
  QuicStreamOffset last_blocked_send_window_offset_;

  // Keep time of the last time a window update was sent.  We use this
  // as part of the receive window auto tuning.
  QuicTime prev_window_update_time_;

  DISALLOW_COPY_AND_ASSIGN(QuicFlowController);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_FLOW_CONTROLLER_H_
