// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_DATAGRAM_CLIENT_SOCKET_H_
#define NET_SOCKET_DATAGRAM_CLIENT_SOCKET_H_

#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"
#include "net/socket/datagram_socket.h"
#include "net/socket/socket.h"

namespace net {

class IPEndPoint;

class NET_EXPORT_PRIVATE DatagramClientSocket : public DatagramSocket,
                                                public Socket {
 public:
  ~DatagramClientSocket() override {}

  // Initialize this socket as a client socket to server at |address|.
  // Returns a network error code.
  virtual int Connect(const IPEndPoint& address) = 0;

  // Binds this socket to |network| and initializes socket as a client socket
  // to server at |address|. All data traffic on the socket will be sent and
  // received via |network|. This call will fail if |network| has disconnected.
  // Communication using this socket will fail if |network| disconnects.
  // Returns a net error code.
  virtual int ConnectUsingNetwork(NetworkChangeNotifier::NetworkHandle network,
                                  const IPEndPoint& address) = 0;

  // Same as ConnectUsingNetwork, except that the current default network is
  // used. Returns a net error code.
  virtual int ConnectUsingDefaultNetwork(const IPEndPoint& address) = 0;

  // Returns the network that either ConnectUsingNetwork() or
  // ConnectUsingDefaultNetwork() bound this socket to. Returns
  // NetworkChangeNotifier::kInvalidNetworkHandle if not explicitly bound via
  // ConnectUsingNetwork() or ConnectUsingDefaultNetwork().
  virtual NetworkChangeNotifier::NetworkHandle GetBoundNetwork() const = 0;

};

}  // namespace net

#endif  // NET_SOCKET_DATAGRAM_CLIENT_SOCKET_H_
