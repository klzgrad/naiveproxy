// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_
#define QUICHE_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_

#include <cstdint>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/core/frames/quic_frame.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/quiche_circular_deque.h"
#include "quiche/common/quiche_linked_hash_map.h"

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
class QUICHE_EXPORT QuicControlFrameManager {
 public:
  class QUICHE_EXPORT DelegateInterface {
   public:
    virtual ~DelegateInterface() = default;

    // Notifies the delegate of errors.
    virtual void OnControlFrameManagerError(QuicErrorCode error_code,
                                            std::string error_details) = 0;

    virtual bool WriteControlFrame(const QuicFrame& frame,
                                   TransmissionType type) = 0;
  };

  explicit QuicControlFrameManager(QuicSession* session);
  QuicControlFrameManager(const QuicControlFrameManager& other) = delete;
  QuicControlFrameManager(QuicControlFrameManager&& other) = delete;
  ~QuicControlFrameManager();

  // Tries to send a WINDOW_UPDATE_FRAME. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferRstStream(QuicControlFrameId id, QuicResetStreamError error,
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
  void WriteOrBufferBlocked(QuicStreamId id, QuicStreamOffset byte_offset);

  // Tries to send a STREAMS_BLOCKED Frame. Buffers the frame if it cannot be
  // sent immediately.
  void WriteOrBufferStreamsBlocked(QuicStreamCount count, bool unidirectional);

  // Tries to send a MAX_STREAMS Frame. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferMaxStreams(QuicStreamCount count, bool unidirectional);

  // Tries to send an IETF-QUIC STOP_SENDING frame. The frame is buffered if it
  // can not be sent immediately.
  void WriteOrBufferStopSending(QuicResetStreamError error,
                                QuicStreamId stream_id);

  // Tries to send an HANDSHAKE_DONE frame. The frame is buffered if it can not
  // be sent immediately.
  void WriteOrBufferHandshakeDone();

  // Tries to send an AckFrequencyFrame. The frame is buffered if it cannot be
  // sent immediately.
  void WriteOrBufferAckFrequency(
      const QuicAckFrequencyFrame& ack_frequency_frame);

  // Tries to send a NEW_CONNECTION_ID frame. The frame is buffered if it cannot
  // be sent immediately.
  void WriteOrBufferNewConnectionId(
      const QuicConnectionId& connection_id, uint64_t sequence_number,
      uint64_t retire_prior_to,
      const StatelessResetToken& stateless_reset_token);

  // Tries to send a RETIRE_CONNNECTION_ID frame. The frame is buffered if it
  // cannot be sent immediately.
  void WriteOrBufferRetireConnectionId(uint64_t sequence_number);

  // Tries to send a NEW_TOKEN frame. Buffers the frame if it cannot be sent
  // immediately.
  void WriteOrBufferNewToken(absl::string_view token);

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

  quiche::QuicheCircularDeque<QuicFrame> control_frames_;

  // Id of latest saved control frame. 0 if no control frame has been saved.
  QuicControlFrameId last_control_frame_id_;

  // The control frame at the 0th index of control_frames_.
  QuicControlFrameId least_unacked_;

  // ID of the least unsent control frame.
  QuicControlFrameId least_unsent_;

  // TODO(fayang): switch to linked_hash_set when chromium supports it. The bool
  // is not used here.
  // Lost control frames waiting to be retransmitted.
  quiche::QuicheLinkedHashMap<QuicControlFrameId, bool>
      pending_retransmissions_;

  DelegateInterface* delegate_;

  // Last sent window update frame for each stream.
  absl::flat_hash_map<QuicStreamId, QuicControlFrameId> window_update_frames_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_
