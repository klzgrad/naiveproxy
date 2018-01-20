// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_NAIVE_NAIVE_CLIENT_H_
#define NET_TOOLS_NAIVE_NAIVE_CLIENT_H_

#include <map>
#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"

namespace net {

class HttpNetworkSession;
class ServerSocket;
class StreamSocket;
class NaiveClientConnection;

class NaiveClient {
 public:
  NaiveClient(std::unique_ptr<ServerSocket> server_socket,
              HttpNetworkSession* session);
  ~NaiveClient();

 private:
  void DoAcceptLoop();
  void OnAcceptComplete(int result);
  void HandleAcceptResult(int result);

  void DoConnect();
  void OnConnectComplete(int connection_id, int result);
  void HandleConnectResult(NaiveClientConnection* connection, int result);

  void DoRun(NaiveClientConnection* connection);
  void OnRunComplete(int connection_id, int result);
  void HandleRunResult(NaiveClientConnection* connection, int result);

  void Close(int connection_id);

  NaiveClientConnection* FindConnection(int connection_id);
  bool HasClosedConnection(NaiveClientConnection* connection);

  std::unique_ptr<ServerSocket> server_socket_;
  HttpNetworkSession* session_;

  unsigned int last_id_;

  std::unique_ptr<StreamSocket> accepted_socket_;

  std::map<unsigned int, std::unique_ptr<NaiveClientConnection>>
      connection_by_id_;

  base::WeakPtrFactory<NaiveClient> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(NaiveClient);
};

}  // namespace net
#endif  // NET_TOOLS_NAIVE_NAIVE_CLIENT_H_
