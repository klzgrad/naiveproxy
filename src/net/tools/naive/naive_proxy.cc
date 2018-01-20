// Copyright 2018 The Chromium Authors. All rights reserved.
// Copyright 2018 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/naive_proxy.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "build/build_config.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_network_session.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_list.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/socket/server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/tools/naive/http_proxy_server_socket.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "net/tools/naive/socks5_server_socket.h"

namespace net {
#if BUILDFLAG(IS_ANDROID)
constexpr int kRotationTimeoutSeconds = 10 * 60;
#else
constexpr int kRotationTimeoutSeconds = 30 * 60;
#endif

NaiveProxy::NaiveProxy(std::unique_ptr<ServerSocket> listen_socket,
                       ClientProtocol protocol,
                       const std::string& listen_user,
                       const std::string& listen_pass,
                       int concurrency,
                       RedirectResolver* resolver,
                       HttpNetworkSession* session,
                       const NetworkTrafficAnnotationTag& traffic_annotation,
                       const std::vector<PaddingType>& supported_padding_types)
    : listen_socket_(std::move(listen_socket)),
      protocol_(protocol),
      listen_user_(listen_user),
      listen_pass_(listen_pass),
      concurrency_(concurrency),
      resolver_(resolver),
      session_(session),
      net_log_(
          NetLogWithSource::Make(session->net_log(), NetLogSourceType::NONE)),
      last_id_(0),
      traffic_annotation_(traffic_annotation),
      supported_padding_types_(supported_padding_types) {
  const auto& proxy_config = static_cast<ConfiguredProxyResolutionService*>(
                                 session_->proxy_resolution_service())
                                 ->config();
  DCHECK(proxy_config);
  const ProxyList& proxy_list =
      proxy_config.value().value().proxy_rules().single_proxies;
  DCHECK(!proxy_list.IsEmpty());
  proxy_info_.UseProxyList(proxy_list);
  proxy_info_.set_traffic_annotation(
      net::MutableNetworkTrafficAnnotationTag(traffic_annotation_));

  for (int i = 0; i < concurrency_; i++) {
    tunnel_ids_.push_back(
        TunnelId{NetworkAnonymizationKey::CreateTransient(), {}});
  }

  DCHECK(listen_socket_);
  // Start accepting connections in next run loop in case when delegate is not
  // ready to get callbacks.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&NaiveProxy::DoAcceptLoop,
                                weak_ptr_factory_.GetWeakPtr()));
}

NaiveProxy::~NaiveProxy() = default;

void NaiveProxy::DoAcceptLoop() {
  int result;
  do {
    result = listen_socket_->Accept(
        &accepted_socket_, base::BindOnce(&NaiveProxy::OnAcceptComplete,
                                          weak_ptr_factory_.GetWeakPtr()));
    if (result == ERR_IO_PENDING) {
      return;
    }
    HandleAcceptResult(result);
  } while (result == OK);
}

void NaiveProxy::OnAcceptComplete(int result) {
  HandleAcceptResult(result);
  if (result == OK) {
    DoAcceptLoop();
  }
}

void NaiveProxy::HandleAcceptResult(int result) {
  if (result != OK) {
    LOG(ERROR) << "Accept error: " << ErrorToShortString(result);
    return;
  }

  session_->CloseIdleConnections("Rotate old tunnels");

  TunnelId& tunnel_id = tunnel_ids_[last_id_ % concurrency_];
  base::Time now = base::Time::Now();
  if (tunnel_id.deadline.is_null()) {
    tunnel_id.deadline = now + base::Seconds(kRotationTimeoutSeconds);
  } else if (now > tunnel_id.deadline) {
    tunnel_id.key = NetworkAnonymizationKey::CreateTransient();
    tunnel_id.deadline = now + base::Seconds(kRotationTimeoutSeconds);
  }

  if (WillCreateSession()) {
    url_getter_ = std::make_unique<PreambleGetter>(proxy_info_, session_,
                                                   current_nak(), net_log_);
    int rv =
        url_getter_->Start(0, base::BindOnce(&NaiveProxy::OnPreambleComplete,
                                             weak_ptr_factory_.GetWeakPtr()));
    if (rv != ERR_IO_PENDING) {
      OnPreambleComplete(rv);
    }
  } else {
    if (url_getter_ != nullptr) {
      url_getter_->StartOne();
    }
    DoConnect();
  }
}

void NaiveProxy::OnPreambleComplete(int result) {
  if (result != OK) {
    LOG(ERROR) << "Preamble error: " << ErrorToShortString(result);
    return;
  }
  DoConnect();
}

void NaiveProxy::DoConnect() {
  auto negotiated_client_padding =
      std::make_unique<PaddingType>(PaddingType::kNone);

  std::unique_ptr<StreamSocket> socket;
  if (protocol_ == ClientProtocol::kSocks5) {
    socket = std::make_unique<Socks5ServerSocket>(std::move(accepted_socket_),
                                                  listen_user_, listen_pass_,
                                                  traffic_annotation_);
  } else if (protocol_ == ClientProtocol::kHttp) {
    negotiated_client_padding =
        std::make_unique<PaddingType>(PaddingType::kNone);
    socket = std::make_unique<HttpProxyServerSocket>(
        std::move(accepted_socket_), listen_user_, listen_pass_,
        negotiated_client_padding.get(), traffic_annotation_,
        supported_padding_types_);
  } else if (protocol_ == ClientProtocol::kRedir) {
    socket = std::move(accepted_socket_);
  } else {
    return;
  }

  auto connection_ptr = std::make_unique<NaiveConnection>(
      last_id_, protocol_, std::move(negotiated_client_padding), proxy_info_,
      resolver_, session_, current_nak(), net_log_, std::move(socket),
      traffic_annotation_);
  auto* connection = connection_ptr.get();
  connection_by_id_[connection->id()] = std::move(connection_ptr);

  ++last_id_;

  int result = connection->Connect(
      base::BindOnce(&NaiveProxy::OnConnectComplete,
                     weak_ptr_factory_.GetWeakPtr(), connection->id()));
  if (result == ERR_IO_PENDING) {
    return;
  }
  HandleConnectResult(connection, result);
}

void NaiveProxy::OnConnectComplete(unsigned int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection) {
    return;
  }
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
  int result = connection->Run(base::BindOnce(&NaiveProxy::OnRunComplete,
                                              weak_ptr_factory_.GetWeakPtr(),
                                              connection->id()));
  if (result == ERR_IO_PENDING) {
    return;
  }
  HandleRunResult(connection, result);
}

void NaiveProxy::OnRunComplete(unsigned int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection) {
    return;
  }
  HandleRunResult(connection, result);
}

void NaiveProxy::HandleRunResult(NaiveConnection* connection, int result) {
  Close(connection->id(), result);
}

void NaiveProxy::Close(unsigned int connection_id, int reason) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end()) {
    return;
  }

  LOG(INFO) << "Connection " << connection_id
            << " closed: " << ErrorToShortString(reason);

  // The call stack might have callbacks which still have the pointer of
  // connection. Instead of referencing connection with ID all the time,
  // destroys the connection in next run loop to make sure any pending
  // callbacks in the call stack return.
  base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
      FROM_HERE, std::move(it->second));
  connection_by_id_.erase(it);
}

NaiveConnection* NaiveProxy::FindConnection(unsigned int connection_id) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end()) {
    return nullptr;
  }
  return it->second.get();
}

const NetworkAnonymizationKey& NaiveProxy::current_nak() const {
  int tunnel_session_id = last_id_ % concurrency_;
  return tunnel_ids_[tunnel_session_id].key;
}

NaiveProxyDelegate* NaiveProxy::naive_proxy_delegate() const {
  auto* proxy_delegate =
      static_cast<NaiveProxyDelegate*>(session_->context().proxy_delegate);
  DCHECK(proxy_delegate);
  return proxy_delegate;
}

bool NaiveProxy::WillCreateSession() const {
  if (proxy_info_.is_direct())
    return false;
  // Simulates HttpProxyConnectJob::CreateSpdySessionKey()
  const ProxyChain& proxy_chain = proxy_info_.proxy_chain();
  auto [last_proxy_partial_chain, last_proxy_server] = proxy_chain.SplitLast();
  if (!last_proxy_server.is_secure_http_like())
    return false;
  const auto& last_proxy_host_port_pair = last_proxy_server.host_port_pair();
  SpdySessionKey key(last_proxy_host_port_pair, PRIVACY_MODE_DISABLED,
                     last_proxy_partial_chain, SessionUsage::kProxy,
                     SocketTag(), current_nak(), SecureDnsPolicy::kDisable,
                     /*disable_cert_verification_network_fetches=*/true);
  return !session_->spdy_session_pool()->FindAvailableSession(
      key, /*enable_ip_based_pooling_for_h2=*/false,
      /*is_websocket=*/false, net_log_);
}
}  // namespace net
