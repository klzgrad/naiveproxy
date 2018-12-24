// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_PROXY_H_
#define NET_TOOLS_NAIVE_NAIVE_PROXY_H_

#include <map>
#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/base/completion_repeating_callback.h"
#include "net/log/net_log_with_source.h"
#include "net/tools/naive/naive_connection.h"

namespace net {

class ClientSocketHandle;
class HttpNetworkSession;
class NaiveConnection;
class ServerSocket;
class StreamSocket;
struct NetworkTrafficAnnotationTag;

class NaiveProxy : public NaiveConnection::Delegate {
 public:
  enum Protocol {
    kSocks5,
    kHttp,
  };

  NaiveProxy(std::unique_ptr<ServerSocket> server_socket,
             Protocol protocol,
             bool use_proxy,
             HttpNetworkSession* session,
             const NetworkTrafficAnnotationTag& traffic_annotation);
  ~NaiveProxy() override;

  int OnConnectServer(unsigned int connection_id,
                      const StreamSocket* accepted_socket,
                      ClientSocketHandle* server_socket,
                      CompletionRepeatingCallback callback) override;

 private:
  void DoAcceptLoop();
  void OnAcceptComplete(int result);
  void HandleAcceptResult(int result);

  void DoConnect();
  void OnConnectComplete(int connection_id, int result);
  void HandleConnectResult(NaiveConnection* connection, int result);

  void DoRun(NaiveConnection* connection);
  void OnRunComplete(int connection_id, int result);
  void HandleRunResult(NaiveConnection* connection, int result);

  void Close(int connection_id, int reason);

  NaiveConnection* FindConnection(int connection_id);
  bool HasClosedConnection(NaiveConnection* connection);

  std::unique_ptr<ServerSocket> listen_socket_;
  Protocol protocol_;
  bool use_proxy_;
  HttpNetworkSession* session_;
  NetLogWithSource net_log_;

  unsigned int last_id_;

  std::unique_ptr<StreamSocket> accepted_socket_;

  std::map<unsigned int, std::unique_ptr<NaiveConnection>> connection_by_id_;

  const NetworkTrafficAnnotationTag& traffic_annotation_;

  base::WeakPtrFactory<NaiveProxy> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NaiveProxy);
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_PROXY_H_
