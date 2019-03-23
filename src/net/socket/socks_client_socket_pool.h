// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_SOCKS_CLIENT_SOCKET_POOL_H_
#define NET_SOCKET_SOCKS_CLIENT_SOCKET_POOL_H_

#include <memory>
#include <string>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/completion_once_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/dns/host_resolver.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/socks_connect_job.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class ConnectJobFactory;
class NetLog;
class SocketPerformanceWatcherFactory;
class TransportClientSocketPool;

class NET_EXPORT_PRIVATE SOCKSClientSocketPool
    : public ClientSocketPool, public HigherLayeredPool {
 public:
  typedef SOCKSSocketParams SocketParams;

  SOCKSClientSocketPool(int max_sockets,
                        int max_sockets_per_group,
                        HostResolver* host_resolver,
                        TransportClientSocketPool* transport_pool,
                        SocketPerformanceWatcherFactory*,
                        NetLog* net_log);

  ~SOCKSClientSocketPool() override;

  // ClientSocketPool implementation.
  int RequestSocket(const std::string& group_name,
                    const void* connect_params,
                    RequestPriority priority,
                    const SocketTag& socket_tag,
                    RespectLimits respect_limits,
                    ClientSocketHandle* handle,
                    CompletionOnceCallback callback,
                    const NetLogWithSource& net_log) override;

  void RequestSockets(const std::string& group_name,
                      const void* params,
                      int num_sockets,
                      const NetLogWithSource& net_log) override;

  void SetPriority(const std::string& group_name,
                   ClientSocketHandle* handle,
                   RequestPriority priority) override;

  void CancelRequest(const std::string& group_name,
                     ClientSocketHandle* handle) override;

  void ReleaseSocket(const std::string& group_name,
                     std::unique_ptr<StreamSocket> socket,
                     int id) override;

  void FlushWithError(int error) override;

  void CloseIdleSockets() override;

  void CloseIdleSocketsInGroup(const std::string& group_name) override;

  int IdleSocketCount() const override;

  int IdleSocketCountInGroup(const std::string& group_name) const override;

  LoadState GetLoadState(const std::string& group_name,
                         const ClientSocketHandle* handle) const override;

  std::unique_ptr<base::DictionaryValue> GetInfoAsValue(
      const std::string& name,
      const std::string& type,
      bool include_nested_pools) const override;

  // LowerLayeredPool implementation.
  bool IsStalled() const override;

  void AddHigherLayeredPool(HigherLayeredPool* higher_pool) override;

  void RemoveHigherLayeredPool(HigherLayeredPool* higher_pool) override;

  // HigherLayeredPool implementation.
  bool CloseOneIdleConnection() override;

 private:
  typedef ClientSocketPoolBase<SOCKSSocketParams> PoolBase;

  class SOCKSConnectJobFactory : public PoolBase::ConnectJobFactory {
   public:
    SOCKSConnectJobFactory(TransportClientSocketPool* transport_pool,
                           HostResolver* host_resolver,
                           NetLog* net_log)
        : transport_pool_(transport_pool),
          host_resolver_(host_resolver),
          net_log_(net_log) {}

    ~SOCKSConnectJobFactory() override {}

    // ClientSocketPoolBase::ConnectJobFactory methods.
    std::unique_ptr<ConnectJob> NewConnectJob(
        const std::string& group_name,
        const PoolBase::Request& request,
        ConnectJob::Delegate* delegate) const override;

   private:
    TransportClientSocketPool* const transport_pool_;
    HostResolver* const host_resolver_;
    NetLog* net_log_;

    DISALLOW_COPY_AND_ASSIGN(SOCKSConnectJobFactory);
  };

  TransportClientSocketPool* const transport_pool_;
  PoolBase base_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSClientSocketPool);
};

}  // namespace net

#endif  // NET_SOCKET_SOCKS_CLIENT_SOCKET_POOL_H_
