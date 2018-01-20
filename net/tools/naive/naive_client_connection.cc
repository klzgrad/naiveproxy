// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_client_connection.h"

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/logging.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/http/http_network_session.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_info.h"
#include "net/proxy/proxy_list.h"
#include "net/proxy/proxy_service.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/socks5_server_socket.h"

namespace net {

namespace {
static const int kBufferSize = 64 * 1024;
}

NaiveClientConnection::NaiveClientConnection(
    int id,
    std::unique_ptr<StreamSocket> accepted_socket,
    HttpNetworkSession* session)
    : id_(id),
      next_state_(STATE_NONE),
      session_(session),
      net_log_(
          NetLogWithSource::Make(session->net_log(), NetLogSourceType::NONE)),
      client_socket_(
          std::make_unique<Socks5ServerSocket>(std::move(accepted_socket))),
      server_socket_handle_(std::make_unique<ClientSocketHandle>()),
      client_error_(OK),
      server_error_(OK),
      full_duplex_(false),
      weak_ptr_factory_(this) {
  io_callback_ = base::Bind(&NaiveClientConnection::OnIOComplete,
                            weak_ptr_factory_.GetWeakPtr());
}

NaiveClientConnection::~NaiveClientConnection() {
  Disconnect();
}

int NaiveClientConnection::Connect(const CompletionCallback& callback) {
  DCHECK(client_socket_);
  DCHECK_EQ(next_state_, STATE_NONE);
  DCHECK(connect_callback_.is_null());

  if (full_duplex_)
    return OK;

  next_state_ = STATE_CONNECT_CLIENT;

  int rv = DoLoop(OK);
  if (rv == ERR_IO_PENDING) {
    connect_callback_ = callback;
  }
  return rv;
}

void NaiveClientConnection::Disconnect() {
  full_duplex_ = false;
  client_socket_->Disconnect();
  if (server_socket_handle_->socket())
    server_socket_handle_->socket()->Disconnect();

  next_state_ = STATE_NONE;
  connect_callback_.Reset();
  run_callback_.Reset();
}

void NaiveClientConnection::DoCallback(int result) {
  DCHECK_NE(result, ERR_IO_PENDING);
  DCHECK(!connect_callback_.is_null());

  // Since Run() may result in Read being called,
  // clear connect_callback_ up front.
  base::ResetAndReturn(&connect_callback_).Run(result);
}

void NaiveClientConnection::OnIOComplete(int result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    DoCallback(rv);
  }
}

int NaiveClientConnection::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, STATE_NONE);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_CONNECT_CLIENT:
        DCHECK_EQ(rv, OK);
        rv = DoConnectClient();
        break;
      case STATE_CONNECT_CLIENT_COMPLETE:
        rv = DoConnectClientComplete(rv);
        break;
      case STATE_CONNECT_SERVER:
        DCHECK_EQ(rv, OK);
        rv = DoConnectServer();
      case STATE_CONNECT_SERVER_COMPLETE:
        rv = DoConnectServerComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);
  return rv;
}

int NaiveClientConnection::DoConnectClient() {
  next_state_ = STATE_CONNECT_CLIENT_COMPLETE;

  return client_socket_->Connect(&request_endpoint_, io_callback_);
}

int NaiveClientConnection::DoConnectClientComplete(int result) {
  if (result < 0)
    return result;

  next_state_ = STATE_CONNECT_SERVER;
  return OK;
}

int NaiveClientConnection::DoConnectServer() {
  ProxyInfo proxy_info;
  const ProxyList& proxy_list =
      session_->proxy_service()->config().proxy_rules().single_proxies;
  if (proxy_list.IsEmpty())
    return ERR_MANDATORY_PROXY_CONFIGURATION_FAILED;
  proxy_info.UseProxyList(proxy_list);

  HttpRequestInfo req_info;
  SSLConfig server_ssl_config;
  SSLConfig proxy_ssl_config;
  session_->GetSSLConfig(req_info, &server_ssl_config, &proxy_ssl_config);
  proxy_ssl_config.rev_checking_enabled = false;

  next_state_ = STATE_CONNECT_SERVER_COMPLETE;

  DCHECK_NE(request_endpoint_.port(), 0);

  LOG(INFO) << "Connection " << id_ << " to " << request_endpoint_.ToString();

  return InitSocketHandleForRawConnect(
      request_endpoint_, session_, proxy_info, server_ssl_config,
      proxy_ssl_config, PRIVACY_MODE_DISABLED, net_log_,
      server_socket_handle_.get(), io_callback_);
}

int NaiveClientConnection::DoConnectServerComplete(int result) {
  if (result < 0)
    return result;

  full_duplex_ = true;
  next_state_ = STATE_NONE;
  return OK;
}

int NaiveClientConnection::Run(const CompletionCallback& callback) {
  DCHECK(client_socket_);
  DCHECK(server_socket_handle_->socket());
  DCHECK_EQ(next_state_, STATE_NONE);
  DCHECK(connect_callback_.is_null());

  run_callback_ = callback;

  Pull(client_socket_.get(), server_socket_handle_->socket());
  Pull(server_socket_handle_->socket(), client_socket_.get());
  return ERR_IO_PENDING;
}

void NaiveClientConnection::Pull(StreamSocket* from, StreamSocket* to) {
  if (client_error_ < 0 || server_error_ < 0)
    return;

  auto buffer = base::MakeRefCounted<IOBuffer>(kBufferSize);
  int rv =
      from->Read(buffer.get(), kBufferSize,
                 base::Bind(&NaiveClientConnection::OnReadComplete,
                            weak_ptr_factory_.GetWeakPtr(), from, to, buffer));

  if (rv != ERR_IO_PENDING)
    OnReadComplete(from, to, buffer, rv);
}

void NaiveClientConnection::Push(StreamSocket* from,
                                 StreamSocket* to,
                                 scoped_refptr<IOBuffer> buffer,
                                 int size) {
  if (client_error_ < 0 || server_error_ < 0)
    return;

  auto drainable = base::MakeRefCounted<DrainableIOBuffer>(buffer.get(), size);
  int rv = to->Write(
      drainable.get(), size,
      base::Bind(&NaiveClientConnection::OnWriteComplete,
                 weak_ptr_factory_.GetWeakPtr(), from, to, drainable));

  if (rv != ERR_IO_PENDING)
    OnWriteComplete(from, to, drainable, rv);
}

void NaiveClientConnection::OnIOError(StreamSocket* socket, int error) {
  if (socket == client_socket_.get()) {
    if (client_error_ == OK) {
      base::ResetAndReturn(&run_callback_).Run(error);
    }
    client_error_ = error;
    return;
  }
  if (socket == server_socket_handle_->socket()) {
    if (server_error_ == OK) {
      base::ResetAndReturn(&run_callback_).Run(error);
    }
    server_error_ = error;
    return;
  }
}

void NaiveClientConnection::OnReadComplete(StreamSocket* from,
                                           StreamSocket* to,
                                           scoped_refptr<IOBuffer> buffer,
                                           int result) {
  if (result <= 0) {
    OnIOError(from, result ? result : ERR_CONNECTION_CLOSED);
    return;
  }

  Push(from, to, buffer, result);
}

void NaiveClientConnection::OnWriteComplete(
    StreamSocket* from,
    StreamSocket* to,
    scoped_refptr<DrainableIOBuffer> drainable,
    int result) {
  if (result < 0) {
    OnIOError(to, result);
    return;
  }

  drainable->DidConsume(result);
  int size = drainable->BytesRemaining();
  if (size > 0) {
    Push(from, to, drainable.get(), size);
    return;
  }

  Pull(from, to);
}

}  // namespace net
