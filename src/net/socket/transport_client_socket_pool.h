// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_
#define NET_SOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/time/time.h"
#include "net/base/net_export.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/socket_tag.h"

namespace net {

class ClientSocketFactory;
class HostResolver;
class NetLog;
class NetLogWithSource;
class SocketPerformanceWatcherFactory;
class TransportSocketParams;

class NET_EXPORT_PRIVATE TransportClientSocketPool : public ClientSocketPool {
 public:
  // Callback to create a ConnectJob using the provided arguments. The lower
  // level parameters used to construct the ConnectJob (like hostname, type of
  // socket, proxy, etc) are all already bound to the callback.  If
  // |websocket_endpoint_lock_manager| is non-null, a ConnectJob for use by
  // WebSockets should be created.
  using CreateConnectJobCallback =
      base::RepeatingCallback<std::unique_ptr<ConnectJob>(
          RequestPriority priority,
          const CommonConnectJobParams& common_connect_job_params,
          ConnectJob::Delegate* delegate)>;

  // "Parameters" that own a single callback for creating a ConnectJob that can
  // be of any type.
  //
  // TODO(mmenke): Once all the socket pool subclasses have been merged, replace
  // this class with a callback.
  class NET_EXPORT_PRIVATE SocketParams
      : public base::RefCounted<SocketParams> {
   public:
    explicit SocketParams(
        const CreateConnectJobCallback& create_connect_job_callback);

    const CreateConnectJobCallback& create_connect_job_callback() {
      return create_connect_job_callback_;
    }

    static scoped_refptr<SocketParams> CreateFromTransportSocketParams(
        scoped_refptr<TransportSocketParams> transport_client_params);

   private:
    friend class base::RefCounted<SocketParams>;
    ~SocketParams();

    const CreateConnectJobCallback create_connect_job_callback_;

    DISALLOW_COPY_AND_ASSIGN(SocketParams);
  };

  TransportClientSocketPool(
      int max_sockets,
      int max_sockets_per_group,
      HostResolver* host_resolver,
      ClientSocketFactory* client_socket_factory,
      SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
      NetLog* net_log);

  ~TransportClientSocketPool() override;

  // ClientSocketPool implementation.
  int RequestSocket(const std::string& group_name,
                    const void* resolve_info,
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

  ClientSocketFactory* client_socket_factory() {
    return client_socket_factory_;
  }

 protected:
  // Methods shared with WebSocketTransportClientSocketPool
  void NetLogTcpClientSocketPoolRequestedSocket(const NetLogWithSource& net_log,
                                                const std::string& group_name);

 private:
  typedef ClientSocketPoolBase<SocketParams> PoolBase;

  class TransportConnectJobFactory
      : public PoolBase::ConnectJobFactory {
   public:
    TransportConnectJobFactory(
        ClientSocketFactory* client_socket_factory,
        HostResolver* host_resolver,
        SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
        NetLog* net_log)
        : client_socket_factory_(client_socket_factory),
          socket_performance_watcher_factory_(
              socket_performance_watcher_factory),
          host_resolver_(host_resolver),
          net_log_(net_log) {}

    ~TransportConnectJobFactory() override {}

    // ClientSocketPoolBase::ConnectJobFactory methods.

    std::unique_ptr<ConnectJob> NewConnectJob(
        const std::string& group_name,
        const PoolBase::Request& request,
        ConnectJob::Delegate* delegate) const override;

   private:
    ClientSocketFactory* const client_socket_factory_;
    SocketPerformanceWatcherFactory* socket_performance_watcher_factory_;
    HostResolver* const host_resolver_;
    NetLog* net_log_;

    DISALLOW_COPY_AND_ASSIGN(TransportConnectJobFactory);
  };

  PoolBase base_;
  ClientSocketFactory* const client_socket_factory_;

  DISALLOW_COPY_AND_ASSIGN(TransportClientSocketPool);
};

}  // namespace net

#endif  // NET_SOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_
