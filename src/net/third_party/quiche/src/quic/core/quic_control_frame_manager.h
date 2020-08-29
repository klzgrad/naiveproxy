// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/frames/quic_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

class QuicSession;

namespace test {
class QuicControlFrameManagerPeer;
}  // namespace test

// Control frame manager contains a list of sent control frames with valid
// control frame IDs. Control frames without valid control frame IDs include:
// (1) non-retransmittable frames (e.g., ACK_FRAME, PADDING_FRAME,
// STOP_WAITING_FRAME, etc.), (2) CONNECTION_CLOSE and IETF Quic
// APPLICATION_CLOSE frames.
// New control frames are added to the tail of the list when they are added to
// the generator. Control frames are removed from the head of the list when they
// get acked. Control frame manager also keeps track of lost control frames
// which need to be retransmitted.
class QUIC_EXPORT_PRIVATE QuicControlFrameManager {
 public:
  explicit QuicControlFrameManager(QuicSession* session);
  QuicControlFrameManager(const QuicControlFrameManager& other) = delete;
  QuicControlFrameManager(QuicControlFrameManager&& other) = delete;
  ~QuicControlFrameManager();

  // Tries to send a WINDOW_UPDATE_FRAME. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferRstStream(QuicControlFrameId id,
                              QuicRstStreamErrorCode error,
                              QuicStreamOffset bytes_written);

  // Tries to send a GOAWAY_FRAME. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferGoAway(QuicErrorCode error,
                           QuicStreamId last_good_stream_id,
                           const std::string& reason);

  // Tries to send a WINDOW_UPDATE_FRAME. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferWindowUpdate(QuicStreamId id, QuicStreamOffset byte_offset);

  // Tries to send a BLOCKED_FRAME. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferBlocked(QuicStreamId id);

  // Tries to send a STREAMS_BLOCKED Frame. Buffers the frame if it cannot be
  // sent immediately.
  void WriteOrBufferStreamsBlocked(QuicStreamCount count, bool unidirectional);

  // Tries to send a MAX_STREAMS Frame. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferMaxStreams(QuicStreamCount count, bool unidirectional);

  // Tries to send an IETF-QUIC STOP_SENDING frame. The frame is buffered if it
  // can not be sent immediately.
  void WriteOrBufferStopSending(uint16_t code, QuicStreamId stream_id);

  // Tries to send an HANDSHAKE_DONE frame. The frame is buffered if it can not
  // be sent immediately.
  void WriteOrBufferHandshakeDone();

  // Sends a PING_FRAME. Do not send PING if there is buffered frames.
  void WritePing();

  // Called when |frame| gets acked. Returns true if |frame| gets acked for the
  // first time, return false otherwise.
  bool OnControlFrameAcked(const QuicFrame& frame);

  // Called when |frame| is considered as lost.
  void OnControlFrameLost(const QuicFrame& frame);

  // Called by the session when the connection becomes writable.
  void OnCanWrite();

  // Retransmit |frame| if it is still outstanding. Returns false if the frame
  // does not get retransmitted because the connection is blocked. Otherwise,
  // returns true.
  bool RetransmitControlFrame(const QuicFrame& frame, TransmissionType type);

  // Returns true if |frame| is outstanding and waiting to be acked. Returns
  // false otherwise.
  bool IsControlFrameOutstanding(const QuicFrame& frame) const;

  // Returns true if there is any lost control frames waiting to be
  // retransmitted.
  bool HasPendingRetransmission() const;

  // Returns true if there are any lost or new control frames waiting to be
  // sent.
  bool WillingToWrite() const;

 private:
  friend class test::QuicControlFrameManagerPeer;

  // Tries to write buffered control frames to the peer.
  void WriteBufferedFrames();

  // Called when |frame| is sent for the first time or gets retransmitted.
  void OnControlFrameSent(const QuicFrame& frame);

  // Writes pending retransmissions if any.
  void WritePendingRetransmission();

  // Called when frame with |id| gets acked. Returns true if |id| gets acked for
  // the first time, return false otherwise.
  bool OnControlFrameIdAcked(QuicControlFrameId id);

  // Retrieves the next pending retransmission. This must only be called when
  // there are pending retransmissions.
  QuicFrame NextPendingRetransmission() const;

  // Returns true if there are buffered frames waiting to be sent for the first
  // time.
  bool HasBufferedFrames() const;

  // Writes or buffers a control frame.  Frame is buffered if there already
  // are frames waiting to be sent. If no others waiting, will try to send the
  // frame.
  void WriteOrBufferQuicFrame(QuicFrame frame);

  QuicCircularDeque<QuicFrame> control_frames_;

  // Id of latest saved control frame. 0 if no control frame has been saved.
  QuicControlFrameId last_control_frame_id_;

  // The control frame at the 0th index of control_frames_.
  QuicControlFrameId least_unacked_;

  // ID of the least unsent control frame.
  QuicControlFrameId least_unsent_;

  // TODO(fayang): switch to linked_hash_set when chromium supports it. The bool
  // is not used here.
  // Lost control frames waiting to be retransmitted.
  QuicLinkedHashMap<QuicControlFrameId, bool> pending_retransmissions_;

  // Pointer to the owning QuicSession object.
  QuicSession* session_;

  // Last sent window update frame for each stream.
  QuicSmallMap<QuicStreamId, QuicControlFrameId, 10> window_update_frames_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_
