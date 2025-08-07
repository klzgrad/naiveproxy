// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_FUZZED_SERVER_SOCKET_H_
#define NET_SOCKET_FUZZED_SERVER_SOCKET_H_

#include <stdint.h>

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_once_callback.h"
#include "net/base/ip_endpoint.h"
#include "net/socket/server_socket.h"

class FuzzedDataProvider;

namespace net {

class NetLog;
class StreamSocket;

// A ServerSocket that uses a FuzzedDataProvider to generate the input the
// server receives. It succeeds in Accept()ing, asynchronously, a single
// connection with that input; later calls to Accept will just return
// ERR_IO_PENDING but will not invoke the callback.
class FuzzedServerSocket : public ServerSocket {
 public:
  // |data_provider| is used as to determine behavior of the socket. It
  // must remain valid until after both this object and the StreamSocket
  // produced by Accept are destroyed.
  FuzzedServerSocket(FuzzedDataProvider* data_provider, net::NetLog* net_log);

  FuzzedServerSocket(const FuzzedServerSocket&) = delete;
  FuzzedServerSocket& operator=(const FuzzedServerSocket&) = delete;

  ~FuzzedServerSocket() override;

  int Listen(const IPEndPoint& address,
             int backlog,
             std::optional<bool> ipv6_only) override;
  int GetLocalAddress(IPEndPoint* address) const override;

  int Accept(std::unique_ptr<StreamSocket>* socket,
             CompletionOnceCallback callback) override;

 private:
  void DispatchAccept(std::unique_ptr<StreamSocket>* socket,
                      CompletionOnceCallback callback);

  raw_ptr<FuzzedDataProvider> data_provider_;
  raw_ptr<net::NetLog> net_log_;

  IPEndPoint listening_on_;
  bool first_accept_ = true;
  bool listen_called_ = false;

  base::WeakPtrFactory<FuzzedServerSocket> weak_factory_{this};
};

}  // namespace net

#endif  // NET_SOCKET_FUZZED_SERVER_SOCKET_H_
