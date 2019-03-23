// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socks_client_socket_pool.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/transport_client_socket_pool.h"

namespace net {

std::unique_ptr<ConnectJob>
SOCKSClientSocketPool::SOCKSConnectJobFactory::NewConnectJob(
    const std::string& group_name,
    const PoolBase::Request& request,
    ConnectJob::Delegate* delegate) const {
  return std::make_unique<SOCKSConnectJob>(
      group_name, request.priority(), request.socket_tag(),
      request.respect_limits() == ClientSocketPool::RespectLimits::ENABLED,
      request.params(), transport_pool_, host_resolver_, delegate, net_log_);
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
