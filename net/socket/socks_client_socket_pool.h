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
#include "base/time/time.h"
#include "net/base/completion_once_callback.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/dns/host_resolver.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/traffic_annotation/network_traffic_annotation.h"

namespace net {

class ConnectJobFactory;
class SocketPerformanceWatcherFactory;
class TransportClientSocketPool;
class TransportSocketParams;

class NET_EXPORT_PRIVATE SOCKSSocketParams
    : public base::RefCounted<SOCKSSocketParams> {
 public:
  SOCKSSocketParams(const scoped_refptr<TransportSocketParams>& proxy_server,
                    bool socks_v5,
                    const HostPortPair& host_port_pair,
                    const NetworkTrafficAnnotationTag& traffic_annotation);

  const scoped_refptr<TransportSocketParams>& transport_params() const {
    return transport_params_;
  }
  const HostResolver::RequestInfo& destination() const { return destination_; }
  bool is_socks_v5() const { return socks_v5_; }

  const NetworkTrafficAnnotationTag traffic_annotation() {
    return traffic_annotation_;
  }

 private:
  friend class base::RefCounted<SOCKSSocketParams>;
  ~SOCKSSocketParams();

  // The transport (likely TCP) connection must point toward the proxy server.
  const scoped_refptr<TransportSocketParams> transport_params_;
  // This is the HTTP destination.
  HostResolver::RequestInfo destination_;
  const bool socks_v5_;

  NetworkTrafficAnnotationTag traffic_annotation_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSSocketParams);
};

// SOCKSConnectJob handles the handshake to a socks server after setting up
// an underlying transport socket.
class SOCKSConnectJob : public ConnectJob {
 public:
  SOCKSConnectJob(const std::string& group_name,
                  RequestPriority priority,
                  const SocketTag& socket_tag,
                  ClientSocketPool::RespectLimits respect_limits,
                  const scoped_refptr<SOCKSSocketParams>& params,
                  const base::TimeDelta& timeout_duration,
                  TransportClientSocketPool* transport_pool,
                  HostResolver* host_resolver,
                  Delegate* delegate,
                  NetLog* net_log);
  ~SOCKSConnectJob() override;

  // ConnectJob methods.
  LoadState GetLoadState() const override;

 private:
  enum State {
    STATE_TRANSPORT_CONNECT,
    STATE_TRANSPORT_CONNECT_COMPLETE,
    STATE_SOCKS_CONNECT,
    STATE_SOCKS_CONNECT_COMPLETE,
    STATE_NONE,
  };

  void OnIOComplete(int result);

  // Runs the state transition loop.
  int DoLoop(int result);

  int DoTransportConnect();
  int DoTransportConnectComplete(int result);
  int DoSOCKSConnect();
  int DoSOCKSConnectComplete(int result);

  // Begins the transport connection and the SOCKS handshake.  Returns OK on
  // success and ERR_IO_PENDING if it cannot immediately service the request.
  // Otherwise, it returns a net error code.
  int ConnectInternal() override;

  scoped_refptr<SOCKSSocketParams> socks_params_;
  TransportClientSocketPool* const transport_pool_;
  HostResolver* const resolver_;

  State next_state_;
  std::unique_ptr<ClientSocketHandle> transport_socket_handle_;
  std::unique_ptr<StreamSocket> socket_;

  DISALLOW_COPY_AND_ASSIGN(SOCKSConnectJob);
};

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

  base::TimeDelta ConnectionTimeout() const override;

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

    base::TimeDelta ConnectionTimeout() const override;

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
