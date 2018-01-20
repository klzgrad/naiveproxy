// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_client.h"

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/net_errors.h"
#include "net/http/http_network_session.h"
#include "net/socket/server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/naive_client_connection.h"
#include "net/tools/naive/socks5_server_socket.h"

namespace net {

NaiveClient::NaiveClient(std::unique_ptr<ServerSocket> server_socket,
                         HttpNetworkSession* session)
    : server_socket_(std::move(server_socket)),
      session_(session),
      last_id_(0),
      weak_ptr_factory_(this) {
  DCHECK(server_socket_);
  // Start accepting connections in next run loop in case when delegate is not
  // ready to get callbacks.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&NaiveClient::DoAcceptLoop,
                                weak_ptr_factory_.GetWeakPtr()));
}

NaiveClient::~NaiveClient() = default;

void NaiveClient::DoAcceptLoop() {
  int result;
  do {
    result = server_socket_->Accept(&accepted_socket_,
                                    base::Bind(&NaiveClient::OnAcceptComplete,
                                               weak_ptr_factory_.GetWeakPtr()));
    if (result == ERR_IO_PENDING)
      return;
    HandleAcceptResult(result);
  } while (result == OK);
}

void NaiveClient::OnAcceptComplete(int result) {
  HandleAcceptResult(result);
  if (result == OK)
    DoAcceptLoop();
}

void NaiveClient::HandleAcceptResult(int result) {
  if (result != OK) {
    LOG(ERROR) << "Accept error: rv=" << result;
    return;
  }
  DoConnect();
}

void NaiveClient::DoConnect() {
  auto connection_ptr = std::make_unique<NaiveClientConnection>(
      ++last_id_, std::move(accepted_socket_), session_);
  auto* connection = connection_ptr.get();
  connection_by_id_[connection->id()] = std::move(connection_ptr);
  int result = connection->Connect(base::Bind(&NaiveClient::OnConnectComplete,
                                              weak_ptr_factory_.GetWeakPtr(),
                                              connection->id()));
  if (result == ERR_IO_PENDING)
    return;
  HandleConnectResult(connection, result);
}

void NaiveClient::OnConnectComplete(int connection_id, int result) {
  NaiveClientConnection* connection = FindConnection(connection_id);
  if (!connection)
    return;
  HandleConnectResult(connection, result);
}

void NaiveClient::HandleConnectResult(NaiveClientConnection* connection,
                                      int result) {
  if (result != OK) {
    Close(connection->id());
    return;
  }
  DoRun(connection);
}

void NaiveClient::DoRun(NaiveClientConnection* connection) {
  int result = connection->Run(base::Bind(&NaiveClient::OnRunComplete,
                                          weak_ptr_factory_.GetWeakPtr(),
                                          connection->id()));
  if (result == ERR_IO_PENDING)
    return;
  HandleRunResult(connection, result);
}

void NaiveClient::OnRunComplete(int connection_id, int result) {
  NaiveClientConnection* connection = FindConnection(connection_id);
  if (!connection)
    return;
  HandleRunResult(connection, result);
}

void NaiveClient::HandleRunResult(NaiveClientConnection* connection,
                                  int result) {
  LOG(INFO) << "Connection " << connection->id()
            << " ended: " << ErrorToString(result);
  Close(connection->id());
}

void NaiveClient::Close(int connection_id) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end())
    return;

  // The call stack might have callbacks which still have the pointer of
  // connection. Instead of referencing connection with ID all the time,
  // destroys the connection in next run loop to make sure any pending
  // callbacks in the call stack return.
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE,
                                                  std::move(it->second));
  connection_by_id_.erase(it);
}

NaiveClientConnection* NaiveClient::FindConnection(int connection_id) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end())
    return nullptr;
  return it->second.get();
}

// This is called after any delegate callbacks are called to check if Close()
// has been called during callback processing. Using the pointer of connection,
// |connection| is safe here because Close() deletes the connection in next run
// loop.
bool NaiveClient::HasClosedConnection(NaiveClientConnection* connection) {
  return FindConnection(connection->id()) != connection;
}

}  // namespace net
