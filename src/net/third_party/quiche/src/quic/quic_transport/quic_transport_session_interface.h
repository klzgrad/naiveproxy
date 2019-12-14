// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_INTERFACE_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_INTERFACE_H_

namespace quic {

// Shared interface between QuicTransportClientSession and
// QuicTransportServerSession.
class QuicTransportSessionInterface {
 public:
  virtual ~QuicTransportSessionInterface() {}

  // Indicates whether the client indication has been sent/received and the
  // connection is thus ready to send/receive application data.  Note that the
  // expectation for this API is that once it becomes true, it will never
  // transition to false.
  virtual bool IsSessionReady() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_INTERFACE_H_
