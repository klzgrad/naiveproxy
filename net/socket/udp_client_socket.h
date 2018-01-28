// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_UDP_CLIENT_SOCKET_H_
#define NET_SOCKET_UDP_CLIENT_SOCKET_H_

#include <stdint.h>

#include "base/macros.h"
#include "net/base/net_export.h"
#include "net/base/rand_callback.h"
#include "net/socket/datagram_client_socket.h"
#include "net/socket/udp_socket.h"

namespace net {

class NetLog;
struct NetLogSource;

// A client socket that uses UDP as the transport layer.
class NET_EXPORT_PRIVATE UDPClientSocket : public DatagramClientSocket {
 public:
  UDPClientSocket(DatagramSocket::BindType bind_type,
                  const RandIntCallback& rand_int_cb,
                  net::NetLog* net_log,
                  const net::NetLogSource& source);
  ~UDPClientSocket() override;

  // DatagramClientSocket implementation.
  int Connect(const IPEndPoint& address) override;
  int ConnectUsingNetwork(NetworkChangeNotifier::NetworkHandle network,
                          const IPEndPoint& address) override;
  int ConnectUsingDefaultNetwork(const IPEndPoint& address) override;
  NetworkChangeNotifier::NetworkHandle GetBoundNetwork() const override;
  int Read(IOBuffer* buf,
           int buf_len,
           const CompletionCallback& callback) override;
  int Write(IOBuffer* buf,
            int buf_len,
            const CompletionCallback& callback) override;
  void Close() override;
  int GetPeerAddress(IPEndPoint* address) const override;
  int GetLocalAddress(IPEndPoint* address) const override;
  void UseNonBlockingIO() override;
  int SetReceiveBufferSize(int32_t size) override;
  int SetSendBufferSize(int32_t size) override;
  int SetDoNotFragment() override;
  const NetLogWithSource& NetLog() const override;

  // Switch to use non-blocking IO. Must be called right after construction and
  // before other calls.
 private:
  UDPSocket socket_;
  NetworkChangeNotifier::NetworkHandle network_;

  DISALLOW_COPY_AND_ASSIGN(UDPClientSocket);
};

}  // namespace net

#endif  // NET_SOCKET_UDP_CLIENT_SOCKET_H_
