// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CLIENT_CONNECTION_H_
#define NET_TOOLS_NAIVE_NAIVE_CLIENT_CONNECTION_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "net/base/completion_callback.h"
#include "net/base/host_port_pair.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy/proxy_info.h"
#include "net/ssl/ssl_config.h"

namespace net {

class ClientSocketHandle;
class DrainableIOBuffer;
class HttpNetworkSession;
class IOBuffer;
class Socks5ServerSocket;
class StreamSocket;

class NaiveClientConnection {
 public:
  using TimeFunc = base::TimeTicks (*)();

  NaiveClientConnection(int id,
                        std::unique_ptr<StreamSocket> accepted_socket,
                        HttpNetworkSession* session);
  ~NaiveClientConnection();

  int id() const { return id_; }
  int Connect(const CompletionCallback& callback);
  void Disconnect();
  int Run(const CompletionCallback& callback);

 private:
  enum State {
    STATE_CONNECT_CLIENT,
    STATE_CONNECT_CLIENT_COMPLETE,
    STATE_CONNECT_SERVER,
    STATE_CONNECT_SERVER_COMPLETE,
    STATE_NONE,
  };

  // From this direction.
  enum Direction {
    kClient = 0,
    kServer = 1,
    kNumDirections = 2,
  };

  void DoCallback(int result);
  void OnIOComplete(int result);
  int DoLoop(int last_io_result);
  int DoConnectClient();
  int DoConnectClientComplete(int result);
  int DoConnectServer();
  int DoConnectServerComplete(int result);
  void Pull(Direction from, Direction to);
  void Push(Direction from,
            Direction to,
            scoped_refptr<IOBuffer> buffer,
            int size);
  void OnIOError(Direction from, int error);
  void OnReadComplete(Direction from,
                      Direction to,
                      scoped_refptr<IOBuffer> buffer,
                      int result);
  void OnWriteComplete(Direction from,
                       Direction to,
                       scoped_refptr<DrainableIOBuffer> drainable,
                       int result);

  int id_;

  CompletionCallback io_callback_;
  CompletionCallback connect_callback_;
  CompletionCallback run_callback_;

  State next_state_;

  HttpNetworkSession* session_;
  NetLogWithSource net_log_;

  HostPortPair request_endpoint_;

  std::unique_ptr<Socks5ServerSocket> client_socket_;
  std::unique_ptr<ClientSocketHandle> server_socket_handle_;

  StreamSocket* sockets_[kNumDirections];
  int errors_[kNumDirections];
  int bytes_passed_without_yielding_[kNumDirections];
  base::TimeTicks yield_after_time_[kNumDirections];

  bool full_duplex_;

  TimeFunc time_func_;

  base::WeakPtrFactory<NaiveClientConnection> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NaiveClientConnection);
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_CLIENT_CONNECTION_H_
