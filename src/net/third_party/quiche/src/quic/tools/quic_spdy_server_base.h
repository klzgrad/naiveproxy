// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A toy server, which connects to a specified port and sends QUIC
// requests to that endpoint.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SPDY_SERVER_BASE_H_
#define QUICHE_QUIC_TOOLS_QUIC_SPDY_SERVER_BASE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

namespace quic {

// Base class for service instances to be used with QuicToyServer.
class QuicSpdyServerBase {
 public:
  virtual ~QuicSpdyServerBase() = default;

  // Creates a UDP socket and listens on |address|. Returns true on success
  // and false otherwise.
  virtual bool CreateUDPSocketAndListen(const QuicSocketAddress& address) = 0;

  // Handles incoming requests. Does not return.
  virtual void HandleEventsForever() = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SPDY_SERVER_BASE_H_
