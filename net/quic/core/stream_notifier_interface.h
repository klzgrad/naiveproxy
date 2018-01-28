// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_STREAM_NOTIFIER_INTERFACE_H_
#define NET_QUIC_CORE_STREAM_NOTIFIER_INTERFACE_H_

#include "net/quic/core/frames/quic_stream_frame.h"
#include "net/quic/core/quic_time.h"

namespace net {

// Pure virtual class to be notified when a packet containing a stream frame is
// acked or lost.
class QUIC_EXPORT_PRIVATE StreamNotifierInterface {
 public:
  virtual ~StreamNotifierInterface() {}

  // Called when |frame| is acked.
  virtual void OnStreamFrameAcked(const QuicStreamFrame& frame,
                                  QuicTime::Delta ack_delay_time) = 0;

  // Called when |frame| is retransmitted.
  virtual void OnStreamFrameRetransmitted(const QuicStreamFrame& frame) = 0;

  // Called when |frame| is discarded from unacked packet map because stream is
  // reset.
  virtual void OnStreamFrameDiscarded(const QuicStreamFrame& frame) = 0;
};

}  // namespace net

#endif  // NET_QUIC_CORE_STREAM_NOTIFIER_INTERFACE_H_
