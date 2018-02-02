// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_
#define NET_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_

#include "net/quic/core/frames/quic_frame.h"
#include "net/quic/platform/api/quic_containers.h"

namespace net {

// Control frame manager contains a list of sent control frames with valid
// control frame IDs. Control frames without valid control frame IDs include:
// (1) non-retransmittable frames (e.g., ACK_FRAME, PADDING_FRAME,
// STOP_WAITING_FRAME, etc.), (2) CONNECTION_CLOSE frame.
// New control frames are added to the tail of the list when they are added to
// the generator. Control frames are removed from the head of the list when they
// get acked. Control frame manager also keeps track of lost control frames
// which need to be retransmitted.
class QUIC_EXPORT_PRIVATE QuicControlFrameManager {
 public:
  QuicControlFrameManager();
  QuicControlFrameManager(const QuicControlFrameManager& other) = delete;
  QuicControlFrameManager(QuicControlFrameManager&& other) = delete;
  ~QuicControlFrameManager();

  // Called when |frame| is sent for the first time or gets retransmitted.
  // Please note, this function should be called when |frame| is added to the
  // generator.
  void OnControlFrameSent(const QuicFrame& frame);

  // Called when |frame| gets acked.
  void OnControlFrameAcked(const QuicFrame& frame);

  // Called when |frame| is considered as lost.
  void OnControlFrameLost(const QuicFrame& frame);

  // Returns true if |frame| is outstanding and waiting to be acked. Returns
  // false otherwise.
  bool IsControlFrameOutstanding(const QuicFrame& frame) const;

  // Returns true if there is any lost control frames waiting to be
  // retransmitted.
  bool HasPendingRetransmission() const;

  // Retrieves the next pending retransmission. This must only be called when
  // there are pending retransmissions.
  QuicFrame NextPendingRetransmission() const;

  size_t size() const;

 private:
  QuicDeque<QuicFrame> control_frames_;

  // The control frame at the 0th index of control_frames_.
  QuicControlFrameId least_unacked_;

  // TODO(fayang): switch to linked_hash_set when chromium supports it. The bool
  // is not used here.
  // Lost control frames waiting to be retransmitted.
  QuicLinkedHashMap<QuicControlFrameId, bool> pending_retransmissions_;
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_CONTROL_FRAME_MANAGER_H_
