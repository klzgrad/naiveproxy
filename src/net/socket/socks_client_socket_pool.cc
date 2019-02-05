// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socks_client_socket_pool.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/socks5_client_socket.h"
#include "net/socket/socks_client_socket.h"
#include "net/socket/transport_client_socket_pool.h"

namespace net {

class NetLog;

SOCKSSocketParams::SOCKSSocketParams(
    const scoped_refptr<TransportSocketParams>& proxy_server,
    bool socks_v5,
    const HostPortPair& host_port_pair,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : transport_params_(proxy_server),
      destination_(host_port_pair),
      socks_v5_(socks_v5),
      traffic_annotation_(traffic_annotation) {}

SOCKSSocketParams::~SOCKSSocketParams() = default;

// SOCKSConnectJobs will time out after this many seconds.  Note this is on
// top of the timeout for the transport socket.
static const int kSOCKSConnectJobTimeoutInSeconds = 30;

SOCKSConnectJob::SOCKSConnectJob(
    const std::string& group_name,
    RequestPriority priority,
    const SocketTag& socket_tag,
    ClientSocketPool::RespectLimits respect_limits,
    const scoped_refptr<SOCKSSocketParams>& socks_params,
    const base::TimeDelta& timeout_duration,
    TransportClientSocketPool* transport_pool,
    HostResolver* host_resolver,
    Delegate* delegate,
    NetLog* net_log)
    : ConnectJob(
          group_name,
          timeout_duration,
          priority,
          socket_tag,
          respect_limits,
          delegate,
          NetLogWithSource::Make(net_log, NetLogSourceType::SOCKS_CONNECT_JOB)),
      socks_params_(socks_params),
      transport_pool_(transport_pool),
      resolver_(host_resolver) {}

SOCKSConnectJob::~SOCKSConnectJob() {
  // We don't worry about cancelling the tcp socket since the destructor in
  // std::unique_ptr<ClientSocketHandle> transport_socket_handle_ will take care
  // of
  // it.
}

LoadState SOCKSConnectJob::GetLoadState() const {
  switch (next_state_) {
    case STATE_TRANSPORT_CONNECT:
    case STATE_TRANSPORT_CONNECT_COMPLETE:
      return transport_socket_handle_->GetLoadState();
    case STATE_SOCKS_CONNECT:
    case STATE_SOCKS_CONNECT_COMPLETE:
      return LOAD_STATE_CONNECTING;
    default:
      NOTREACHED();
      return LOAD_STATE_IDLE;
  }
}

void SOCKSConnectJob::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    NotifyDelegateOfCompletion(rv);  // Deletes |this|
}

int SOCKSConnectJob::DoLoop(int result) {
  DCHECK_NE(next_state_, STATE_NONE);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_TRANSPORT_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoTransportConnect();
        break;
      case STATE_TRANSPORT_CONNECT_COMPLETE:
        rv = DoTransportConnectComplete(rv);
        break;
      case STATE_SOCKS_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoSOCKSConnect();
        break;
      case STATE_SOCKS_CONNECT_COMPLETE:
        rv = DoSOCKSConnectComplete(rv);
        break;
      default:
        NOTREACHED() << "bad state";
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE);

  return rv;
}

int SOCKSConnectJob::DoTransportConnect() {
  next_state_ = STATE_TRANSPORT_CONNECT_COMPLETE;
  transport_socket_handle_.reset(new ClientSocketHandle());
  CompletionOnceCallback callback =
      base::BindOnce(&SOCKSConnectJob::OnIOComplete, base::Unretained(this));
  return transport_socket_handle_->Init(
      group_name(), socks_params_->transport_params(), priority(), socket_tag(),
      respect_limits(), std::move(callback), transport_pool_, net_log());
}

int SOCKSConnectJob::DoTransportConnectComplete(int result) {
  if (result != OK)
    return ERR_PROXY_CONNECTION_FAILED;

  // Reset the timer to just the length of time allowed for SOCKS handshake
  // so that a fast TCP connection plus a slow SOCKS failure doesn't take
  // longer to timeout than it should.
  ResetTimer(base::TimeDelta::FromSeconds(kSOCKSConnectJobTimeoutInSeconds));
  next_state_ = STATE_SOCKS_CONNECT;
  return result;
}

int SOCKSConnectJob::DoSOCKSConnect() {
  next_state_ = STATE_SOCKS_CONNECT_COMPLETE;

  // Add a SOCKS connection on top of the tcp socket.
  if (socks_params_->is_socks_v5()) {
    socket_.reset(new SOCKS5ClientSocket(std::move(transport_socket_handle_),
                                         socks_params_->destination(),
                                         socks_params_->traffic_annotation()));
  } else {
    socket_.reset(new SOCKSClientSocket(
        std::move(transport_socket_handle_), socks_params_->destination(),
        priority(), resolver_, socks_params_->traffic_annotation()));
  }
  return socket_->Connect(
      base::Bind(&SOCKSConnectJob::OnIOComplete, base::Unretained(this)));
}

int SOCKSConnectJob::DoSOCKSConnectComplete(int result) {
  if (result != OK) {
    socket_->Disconnect();
    return result;
  }

  SetSocket(std::move(socket_));
  return result;
}

int SOCKSConnectJob::ConnectInternal() {
  next_state_ = STATE_TRANSPORT_CONNECT;
  return DoLoop(OK);
}

void SOCKSConnectJob::ChangePriorityInternal(RequestPriority priority) {
  // Currently doesn't change host resolution request priority for SOCKS4 case.
  if (transport_socket_handle_)
    transport_socket_handle_->SetPriority(priority);
}

std::unique_ptr<ConnectJob>
SOCKSClientSocketPool::SOCKSConnectJobFactory::NewConnectJob(
    const std::string& group_name,
    const PoolBase::Request& request,
    ConnectJob::Delegate* delegate) const {
  return std::unique_ptr<ConnectJob>(new SOCKSConnectJob(
      group_name, request.priority(), request.socket_tag(),
      request.respect_limits(), request.params(), ConnectionTimeout(),
      transport_pool_, host_resolver_, delegate, net_log_));
}

base::TimeDelta
SOCKSClientSocketPool::SOCKSConnectJobFactory::ConnectionTimeout() const {
  return transport_pool_->ConnectionTimeout() +
      base::TimeDelta::FromSeconds(kSOCKSConnectJobTimeoutInSeconds);
}

SOCKSClientSocketPool::SOCKSClientSocketPool(
    int max_sockets,
    int max_sockets_per_group,
    HostResolver* host_resolver,
    TransportClientSocketPool* transport_pool,
    SocketPerformanceWatcherFactory*,
    NetLog* net_log)
    : transport_pool_(transport_pool),
      base_(
          this,
          max_sockets,
          max_sockets_per_group,
          ClientSocketPool::unused_idle_socket_timeout(),
          ClientSocketPool::used_idle_socket_timeout(),
          new SOCKSConnectJobFactory(transport_pool, host_resolver, net_log)) {
  // We should always have a |transport_pool_| except in unit tests.
  if (transport_pool_)
    base_.AddLowerLayeredPool(transport_pool_);
}

SOCKSClientSocketPool::~SOCKSClientSocketPool() = default;

int SOCKSClientSocketPool::RequestSocket(const std::string& group_name,
                                         const void* socket_params,
                                         RequestPriority priority,
                                         const SocketTag& socket_tag,
                                         RespectLimits respect_limits,
                                         ClientSocketHandle* handle,
                                         CompletionOnceCallback callback,
                                         const NetLogWithSource& net_log) {
  const scoped_refptr<SOCKSSocketParams>* casted_socket_params =
      static_cast<const scoped_refptr<SOCKSSocketParams>*>(socket_params);

  return base_.RequestSocket(group_name, *casted_socket_params, priority,
                             socket_tag, respect_limits, handle,
                             std::move(callback), net_log);
}

void SOCKSClientSocketPool::RequestSockets(const std::string& group_name,
                                           const void* params,
                                           int num_sockets,
                                           const NetLogWithSource& net_log) {
  const scoped_refptr<SOCKSSocketParams>* casted_params =
      static_cast<const scoped_refptr<SOCKSSocketParams>*>(params);

  base_.RequestSockets(group_name, *casted_params, num_sockets, net_log);
}

void SOCKSClientSocketPool::SetPriority(const std::string& group_name,
                                        ClientSocketHandle* handle,
                                        RequestPriority priority) {
  base_.SetPriority(group_name, handle, priority);
}

void SOCKSClientSocketPool::CancelRequest(const std::string& group_name,
                                          ClientSocketHandle* handle) {
  base_.CancelRequest(group_name, handle);
}

void SOCKSClientSocketPool::ReleaseSocket(const std::string& group_name,
                                          std::unique_ptr<StreamSocket> socket,
                                          int id) {
  base_.ReleaseSocket(group_name, std::move(socket), id);
}

void SOCKSClientSocketPool::FlushWithError(int error) {
  base_.FlushWithError(error);
}

void SOCKSClientSocketPool::CloseIdleSockets() {
  base_.CloseIdleSockets();
}

void SOCKSClientSocketPool::CloseIdleSocketsInGroup(
    const std::string& group_name) {
  base_.CloseIdleSocketsInGroup(group_name);
}

int SOCKSClientSocketPool::IdleSocketCount() const {
  return base_.idle_socket_count();
}

int SOCKSClientSocketPool::IdleSocketCountInGroup(
    const std::string& group_name) const {
  return base_.IdleSocketCountInGroup(group_name);
}

LoadState SOCKSClientSocketPool::GetLoadState(
    const std::string& group_name, const ClientSocketHandle* handle) const {
  return base_.GetLoadState(group_name, handle);
}

std::unique_ptr<base::DictionaryValue> SOCKSClientSocketPool::GetInfoAsValue(
    const std::string& name,
    const std::string& type,
    bool include_nested_pools) const {
  std::unique_ptr<base::DictionaryValue> dict(base_.GetInfoAsValue(name, type));
  if (include_nested_pools) {
    std::unique_ptr<base::ListValue> list(new base::ListValue());
    list->Append(transport_pool_->GetInfoAsValue("transport_socket_pool",
                                                 "transport_socket_pool",
                                                 false));
    dict->Set("nested_pools", std::move(list));
  }
  return dict;
}

base::TimeDelta SOCKSClientSocketPool::ConnectionTimeout() const {
  return base_.ConnectionTimeout();
}

bool SOCKSClientSocketPool::IsStalled() const {
  return base_.IsStalled();
}

void SOCKSClientSocketPool::AddHigherLayeredPool(
    HigherLayeredPool* higher_pool) {
  base_.AddHigherLayeredPool(higher_pool);
}

void SOCKSClientSocketPool::RemoveHigherLayeredPool(
    HigherLayeredPool* higher_pool) {
  base_.RemoveHigherLayeredPool(higher_pool);
}

bool SOCKSClientSocketPool::CloseOneIdleConnection() {
  if (base_.CloseOneIdleSocket())
    return true;
  return base_.CloseOneIdleConnectionInHigherLayeredPool();
}

}  // namespace net
