// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/fuzzed_server_socket.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/socket/fuzzed_socket.h"

namespace net {

FuzzedServerSocket::FuzzedServerSocket(base::FuzzedDataProvider* data_provider,
                                       net::NetLog* net_log)
    : data_provider_(data_provider),
      net_log_(net_log),
      first_accept_(true),
      listen_called_(false),
      weak_factory_(this) {}

FuzzedServerSocket::~FuzzedServerSocket() = default;

int FuzzedServerSocket::Listen(const IPEndPoint& address, int backlog) {
  DCHECK(!listen_called_);
  listening_on_ = address;
  listen_called_ = true;
  return OK;
}

int FuzzedServerSocket::GetLocalAddress(IPEndPoint* address) const {
  *address = listening_on_;
  return OK;
}

int FuzzedServerSocket::Accept(std::unique_ptr<StreamSocket>* socket,
                               const CompletionCallback& callback) {
  if (first_accept_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE, base::Bind(&FuzzedServerSocket::DispatchAccept,
                              weak_factory_.GetWeakPtr(), socket, callback));
  }
  first_accept_ = false;

  return ERR_IO_PENDING;
}

void FuzzedServerSocket::DispatchAccept(std::unique_ptr<StreamSocket>* socket,
                                        const CompletionCallback& callback) {
  std::unique_ptr<FuzzedSocket> connected_socket(
      std::make_unique<FuzzedSocket>(data_provider_, net_log_));
  // The Connect call should always succeed synchronously, without using the
  // callback, since connected_socket->set_fuzz_connect_result(true) has not
  // been called.
  CHECK_EQ(net::OK, connected_socket->Connect(callback));
  *socket = std::move(connected_socket);
  callback.Run(OK);
}

}  // namespace net
