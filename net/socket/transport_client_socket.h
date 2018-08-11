// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_TRANSPORT_CLIENT_SOCKET_H_
#define NET_SOCKET_TRANSPORT_CLIENT_SOCKET_H_

#include "net/base/ip_endpoint.h"
#include "net/base/net_export.h"
#include "net/socket/stream_socket.h"

namespace net {

// A socket class that extends StreamSocket to provide methods that are relevant
// to a transport client socket.
class NET_EXPORT_PRIVATE TransportClientSocket : public StreamSocket {
 public:
  ~TransportClientSocket() override {}

  // Binds the socket to a local address, |local_addr|. Returns OK on success,
  // and a net error code on failure.
  virtual int Bind(const net::IPEndPoint& local_addr) = 0;
};

}  // namespace net

#endif  // NET_SOCKET_TRANSPORT_CLIENT_SOCKET_H_
