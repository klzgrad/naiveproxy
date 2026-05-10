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
#include "base/time/time.h"
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
namespace {
constexpr base::TimeDelta kIdleCheckPeriod = base::Minutes(1);
}  // namespace

NaiveProxy::Tunnel::Tunnel() = default;
NaiveProxy::Tunnel::~Tunnel() = default;

NaiveProxy::NaiveProxy(std::unique_ptr<ServerSocket> listen_socket,
                       ClientProtocol protocol,
                       const std::string& listen_user,
                       const std::string& listen_pass,
                       int concurrency,
                       int tunnel_timeout,
                       int idle_timeout,
                       RedirectResolver* resolver,
                       HttpNetworkSession* session,
                       const NetworkTrafficAnnotationTag& traffic_annotation,
                       const std::vector<PaddingType>& supported_padding_types)
    : listen_socket_(std::move(listen_socket)),
      protocol_(protocol),
      listen_user_(listen_user),
      listen_pass_(listen_pass),
      concurrency_(concurrency),
      tunnel_timeout_(base::Seconds(tunnel_timeout)),
      idle_timeout_(base::Seconds(idle_timeout)),
      resolver_(resolver),
      session_(session),
      net_log_(
          NetLogWithSource::Make(session->net_log(), NetLogSourceType::NONE)),
      next_id_(0),
      next_state_(State::kAccept),
      tunnels_(concurrency),
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

  DCHECK(listen_socket_);
  // Start accepting connections in next run loop in case when delegate is not
  // ready to get callbacks.
  io_callback_ = base::BindRepeating(&NaiveProxy::OnIOComplete,
                                     weak_ptr_factory_.GetWeakPtr());
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&NaiveProxy::OnIOComplete,
                                weak_ptr_factory_.GetWeakPtr(), OK));

  cleanup_timer_.Start(FROM_HERE, kIdleCheckPeriod, this,
                       &NaiveProxy::CleanUpIdleConnections);
}

NaiveProxy::~NaiveProxy() = default;

void NaiveProxy::OnIOComplete(int result) {
  DCHECK_NE(next_state_, State::kNone);
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&NaiveProxy::OnIOComplete,
                                  weak_ptr_factory_.GetWeakPtr(), OK));
  }
}

int NaiveProxy::DoLoop(int last_io_result) {
  DCHECK_NE(next_state_, State::kNone);
  int rv = last_io_result;
  do {
    State state = next_state_;
    next_state_ = State::kNone;
    switch (state) {
      case State::kAccept:
        DCHECK_EQ(OK, rv);
        rv = DoAccept();
        break;
      case State::kAcceptComplete:
        rv = DoAcceptComplete(rv);
        break;
      case State::kPreamble:
        DCHECK_EQ(OK, rv);
        rv = DoPreamble();
        break;
      case State::kPreambleComplete:
        rv = DoPreambleComplete(rv);
        break;
      case State::kConnect:
        DCHECK_EQ(OK, rv);
        rv = DoConnect();
        break;
      default:
        rv = ERR_UNEXPECTED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != State::kNone);
  return rv;
}

int NaiveProxy::DoAccept() {
  next_state_ = State::kAcceptComplete;
  return listen_socket_->Accept(&accepted_socket_, io_callback_);
}

int NaiveProxy::DoAcceptComplete(int result) {
  if (result != OK) {
    next_state_ = State::kAccept;
    LOG(ERROR) << "Accept error: " << ErrorToShortString(result);
    // This accept error is ignored to start the next accept.
    return OK;
  }

  Tunnel& tunnel = tunnels_[next_id_ % concurrency_];
  base::TimeTicks now = base::TimeTicks::Now();
  if (tunnel.deadline.is_null()) {
    tunnel.deadline = now + tunnel_timeout_;
    next_state_ = State::kPreamble;
  } else if (now > tunnel.deadline) {
    tunnel.nak = NetworkAnonymizationKey::CreateTransient();
    tunnel.deadline = now + tunnel_timeout_;
    tunnel.url_getter.reset();
    next_state_ = State::kPreamble;
  } else {
    DCHECK(tunnel.url_getter != nullptr);
    tunnel.url_getter->StartOne();
    next_state_ = State::kConnect;
  }
  return OK;
}

// Possible exit states: State::kAccept, State::kPreambleComplete
int NaiveProxy::DoPreamble() {
  Tunnel& tunnel = tunnels_[next_id_ % concurrency_];
  DCHECK(WillCreateSession(tunnel.nak));
  tunnel.url_getter = std::make_unique<PreambleGetter>(proxy_info_, session_,
                                                       tunnel.nak, net_log_);
  next_state_ = State::kPreambleComplete;
  return tunnel.url_getter->Start(io_callback_);
}

int NaiveProxy::DoPreambleComplete(int result) {
  if (result != OK) {
    LOG(WARNING) << "Preamble error: " << ErrorToShortString(result);
    // Preamble error doesn't prevent Connect().
  }
  next_state_ = State::kConnect;
  return OK;
}

int NaiveProxy::DoConnect() {
  auto negotiated_client_padding =
      std::make_unique<PaddingType>(PaddingType::kNone);

  // Once accepted_socket_ is moved, the next Accept can start.
  next_state_ = State::kAccept;

  std::unique_ptr<StreamSocket> socket;
  if (protocol_ == ClientProtocol::kSocks5) {
    socket = std::make_unique<Socks5ServerSocket>(std::move(accepted_socket_),
                                                  listen_user_, listen_pass_,
                                                  traffic_annotation_);
  } else if (protocol_ == ClientProtocol::kHttp) {
    socket = std::make_unique<HttpProxyServerSocket>(
        std::move(accepted_socket_), listen_user_, listen_pass_,
        negotiated_client_padding.get(), traffic_annotation_,
        supported_padding_types_);
  } else if (protocol_ == ClientProtocol::kRedir) {
    socket = std::move(accepted_socket_);
  } else {
    return OK;
  }

  const Tunnel& tunnel = tunnels_[next_id_ % concurrency_];
  auto connection_ptr = std::make_unique<NaiveConnection>(
      next_id_, protocol_, std::move(negotiated_client_padding), proxy_info_,
      resolver_, session_, tunnel.nak, net_log_, std::move(socket),
      traffic_annotation_);
  auto* connection = connection_ptr.get();
  connection_by_id_[connection->id()] = std::move(connection_ptr);

  ++next_id_;

  int result = connection->Connect(
      base::BindOnce(&NaiveProxy::OnConnectComplete,
                     weak_ptr_factory_.GetWeakPtr(), connection->id()));
  if (result == ERR_IO_PENDING) {
    // Connect result doesn't prevent the next Accept
    return OK;
  }
  HandleConnectResult(connection, result);
  return OK;
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

NaiveProxyDelegate* NaiveProxy::naive_proxy_delegate() const {
  auto* proxy_delegate =
      static_cast<NaiveProxyDelegate*>(session_->context().proxy_delegate);
  DCHECK(proxy_delegate);
  return proxy_delegate;
}

bool NaiveProxy::WillCreateSession(const NetworkAnonymizationKey& nak) const {
  if (proxy_info_.is_direct()) {
    return false;
  }
  // Simulates HttpProxyConnectJob::CreateSpdySessionKey()
  const ProxyChain& proxy_chain = proxy_info_.proxy_chain();
  auto [last_proxy_partial_chain, last_proxy_server] = proxy_chain.SplitLast();
  if (!last_proxy_server.is_secure_http_like()) {
    return false;
  }
  const auto& last_proxy_host_port_pair = last_proxy_server.host_port_pair();
  SpdySessionKey key(last_proxy_host_port_pair, PRIVACY_MODE_DISABLED,
                     last_proxy_partial_chain, SessionUsage::kProxy,
                     SocketTag(), nak, SecureDnsPolicy::kDisable,
                     /*disable_cert_verification_network_fetches=*/true);
  return !session_->spdy_session_pool()->FindAvailableSession(
      key, /*enable_ip_based_pooling_for_h2=*/false,
      /*is_websocket=*/false, net_log_);
}

void NaiveProxy::CleanUpIdleConnections() {
  std::vector<NaiveConnection*> idle_conns;
  base::TimeTicks now = base::TimeTicks::Now();
  for (const auto& [id, conn] : connection_by_id_) {
    base::TimeDelta idle = now - conn->GetLastWriteTime();
    base::TimeDelta age = now - conn->GetCreationTime();
    if (idle > idle_timeout_ || age > tunnel_timeout_) {
      idle_conns.push_back(conn.get());
    }
  }
  for (NaiveConnection* conn : idle_conns) {
    conn->Disconnect();
  }
  session_->CloseIdleConnections("Rotate old tunnels");
}
}  // namespace net
