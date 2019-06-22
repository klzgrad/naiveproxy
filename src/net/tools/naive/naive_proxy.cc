// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_proxy.h"

#include <utility>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/net_errors.h"
#include "net/http/http_network_session.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_list.h"
#include "net/proxy_resolution/proxy_resolution_service.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/http_proxy_socket.h"
#include "net/tools/naive/socks5_server_socket.h"

namespace net {

NaiveProxy::NaiveProxy(std::unique_ptr<ServerSocket> listen_socket,
                       NaiveConnection::Protocol protocol,
                       bool use_padding,
                       HttpNetworkSession* session,
                       const NetworkTrafficAnnotationTag& traffic_annotation)
    : listen_socket_(std::move(listen_socket)),
      protocol_(protocol),
      use_padding_(use_padding),
      session_(session),
      net_log_(
          NetLogWithSource::Make(session->net_log(), NetLogSourceType::NONE)),
      last_id_(0),
      traffic_annotation_(traffic_annotation),
      weak_ptr_factory_(this) {
  const auto& proxy_config = session_->proxy_resolution_service()->config();
  DCHECK(proxy_config);
  const ProxyList& proxy_list =
      proxy_config.value().value().proxy_rules().single_proxies;
  DCHECK(!proxy_list.IsEmpty());
  proxy_info_.UseProxyList(proxy_list);
  proxy_info_.set_traffic_annotation(
      net::MutableNetworkTrafficAnnotationTag(traffic_annotation_));

  HttpRequestInfo req_info;
  session_->GetSSLConfig(req_info, &server_ssl_config_, &proxy_ssl_config_);
  proxy_ssl_config_.disable_cert_verification_network_fetches = true;

  DCHECK(listen_socket_);
  // Start accepting connections in next run loop in case when delegate is not
  // ready to get callbacks.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(&NaiveProxy::DoAcceptLoop,
                                weak_ptr_factory_.GetWeakPtr()));
}

NaiveProxy::~NaiveProxy() = default;

void NaiveProxy::DoAcceptLoop() {
  int result;
  do {
    result = listen_socket_->Accept(
        &accepted_socket_, base::BindRepeating(&NaiveProxy::OnAcceptComplete,
                                               weak_ptr_factory_.GetWeakPtr()));
    if (result == ERR_IO_PENDING)
      return;
    HandleAcceptResult(result);
  } while (result == OK);
}

void NaiveProxy::OnAcceptComplete(int result) {
  HandleAcceptResult(result);
  if (result == OK)
    DoAcceptLoop();
}

void NaiveProxy::HandleAcceptResult(int result) {
  if (result != OK) {
    LOG(ERROR) << "Accept error: rv=" << result;
    return;
  }
  DoConnect();
}

void NaiveProxy::DoConnect() {
  std::unique_ptr<StreamSocket> socket;
  NaiveConnection::Direction pad_direction;
  if (protocol_ == NaiveConnection::kSocks5) {
    socket = std::make_unique<Socks5ServerSocket>(std::move(accepted_socket_),
                                                  traffic_annotation_);
    pad_direction = NaiveConnection::kClient;
  } else if (protocol_ == NaiveConnection::kHttp) {
    socket = std::make_unique<HttpProxySocket>(std::move(accepted_socket_),
                                               traffic_annotation_);
    pad_direction = NaiveConnection::kServer;
  } else if (protocol_ == NaiveConnection::kRedir) {
    socket = std::move(accepted_socket_);
    pad_direction = NaiveConnection::kClient;
  } else {
    return;
  }
  if (!use_padding_) {
    pad_direction = NaiveConnection::kNone;
  }
  auto connection_ptr = std::make_unique<NaiveConnection>(
      ++last_id_, protocol_, pad_direction, proxy_info_, server_ssl_config_,
      proxy_ssl_config_, session_, net_log_, std::move(socket),
      traffic_annotation_);
  auto* connection = connection_ptr.get();
  connection_by_id_[connection->id()] = std::move(connection_ptr);
  int result = connection->Connect(
      base::BindRepeating(&NaiveProxy::OnConnectComplete,
                          weak_ptr_factory_.GetWeakPtr(), connection->id()));
  if (result == ERR_IO_PENDING)
    return;
  HandleConnectResult(connection, result);
}

void NaiveProxy::OnConnectComplete(unsigned int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection)
    return;
  HandleConnectResult(connection, result);
}

void NaiveProxy::HandleConnectResult(NaiveConnection* connection, int result) {
  if (result != OK) {
    Close(connection->id(), result);
    return;
  }
  DoRun(connection);
}

void NaiveProxy::DoRun(NaiveConnection* connection) {
  int result = connection->Run(
      base::BindRepeating(&NaiveProxy::OnRunComplete,
                          weak_ptr_factory_.GetWeakPtr(), connection->id()));
  if (result == ERR_IO_PENDING)
    return;
  HandleRunResult(connection, result);
}

void NaiveProxy::OnRunComplete(unsigned int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection)
    return;
  HandleRunResult(connection, result);
}

void NaiveProxy::HandleRunResult(NaiveConnection* connection, int result) {
  Close(connection->id(), result);
}

void NaiveProxy::Close(unsigned int connection_id, int reason) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end())
    return;

  LOG(INFO) << "Connection " << connection_id
            << " closed: " << ErrorToShortString(reason);

  // The call stack might have callbacks which still have the pointer of
  // connection. Instead of referencing connection with ID all the time,
  // destroys the connection in next run loop to make sure any pending
  // callbacks in the call stack return.
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE,
                                                  std::move(it->second));
  connection_by_id_.erase(it);
}

NaiveConnection* NaiveProxy::FindConnection(unsigned int connection_id) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end())
    return nullptr;
  return it->second.get();
}

}  // namespace net
