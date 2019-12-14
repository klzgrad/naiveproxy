// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_SESSION_NOTIFIER_INTERFACE_H_
#define QUICHE_QUIC_CORE_SESSION_NOTIFIER_INTERFACE_H_

#include "net/third_party/quiche/src/quic/core/frames/quic_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_time.h"

namespace quic {

// Pure virtual class to be notified when a packet containing a frame is acked
// or lost.
class QUIC_EXPORT_PRIVATE SessionNotifierInterface {
 public:
  virtual ~SessionNotifierInterface() {}

  // Called when |frame| is acked. Returns true if any new data gets acked,
  // returns false otherwise.
  virtual bool OnFrameAcked(const QuicFrame& frame,
                            QuicTime::Delta ack_delay_time,
                            QuicTime receive_timestamp) = 0;

  // Called when |frame| is retransmitted.
  virtual void OnStreamFrameRetransmitted(const QuicStreamFrame& frame) = 0;

  // Called when |frame| is considered as lost.
  virtual void OnFrameLost(const QuicFrame& frame) = 0;

  // Called to retransmit |frames| with transmission |type|.
  virtual void RetransmitFrames(const QuicFrames& frames,
                                TransmissionType type) = 0;

  // Returns true if |frame| is outstanding and waiting to be acked.
  virtual bool IsFrameOutstanding(const QuicFrame& frame) const = 0;

  // Returns true if crypto stream is waiting for acks.
  virtual bool HasUnackedCryptoData() const = 0;

  // Returns true if any stream is waiting for acks.
  virtual bool HasUnackedStreamData() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_SESSION_NOTIFIER_INTERFACE_H_
