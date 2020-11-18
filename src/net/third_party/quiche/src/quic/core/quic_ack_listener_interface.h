// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_ACK_LISTENER_INTERFACE_H_
#define QUICHE_QUIC_CORE_QUIC_ACK_LISTENER_INTERFACE_H_

#include "net/third_party/quiche/src/quic/core/quic_time.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"

namespace quic {

// Pure virtual class to listen for packet acknowledgements.
class QUIC_EXPORT_PRIVATE QuicAckListenerInterface
    : public QuicReferenceCounted {
 public:
  QuicAckListenerInterface() {}

  // Called when a packet is acked.  Called once per packet.
  // |acked_bytes| is the number of data bytes acked.
  virtual void OnPacketAcked(int acked_bytes,
                             QuicTime::Delta ack_delay_time) = 0;

  // Called when a packet is retransmitted.  Called once per packet.
  // |retransmitted_bytes| is the number of data bytes retransmitted.
  virtual void OnPacketRetransmitted(int retransmitted_bytes) = 0;

 protected:
  // Delegates are ref counted.
  ~QuicAckListenerInterface() override;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_ACK_LISTENER_INTERFACE_H_
