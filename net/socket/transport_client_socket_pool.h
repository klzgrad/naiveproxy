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
#include "base/timer/timer.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/dns/host_resolver.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/connection_attempts.h"
#include "net/socket/socket_tag.h"

namespace net {

class ClientSocketFactory;
class SocketPerformanceWatcherFactory;
class NetLog;
class NetLogWithSource;

typedef base::Callback<int(const AddressList&, const NetLogWithSource& net_log)>
    OnHostResolutionCallback;

class NET_EXPORT_PRIVATE TransportSocketParams
    : public base::RefCounted<TransportSocketParams> {
 public:
  // CombineConnectAndWrite currently translates to using TCP FastOpen.
  // TCP FastOpen should not be used if the first write to the socket may
  // be non-idempotent, as the underlying socket could retransmit the data
  // on failure of the first transmission.
  enum CombineConnectAndWritePolicy {
    COMBINE_CONNECT_AND_WRITE_DEFAULT,  // Default policy, don't combine.
    COMBINE_CONNECT_AND_WRITE_DESIRED,  // Combine if supported by socket.
  };

  // |host_resolution_callback| will be invoked after the the hostname is
  // resolved.  If |host_resolution_callback| does not return OK, then the
  // connection will be aborted with that value. |combine_connect_and_write|
  // defines the policy for use of TCP FastOpen on this socket.
  TransportSocketParams(
      const HostPortPair& host_port_pair,
      bool disable_resolver_cache,
      const OnHostResolutionCallback& host_resolution_callback,
      CombineConnectAndWritePolicy combine_connect_and_write);

  const HostResolver::RequestInfo& destination() const { return destination_; }
  const OnHostResolutionCallback& host_resolution_callback() const {
    return host_resolution_callback_;
  }

  CombineConnectAndWritePolicy combine_connect_and_write() const {
    return combine_connect_and_write_;
  }

 private:
  friend class base::RefCounted<TransportSocketParams>;
  ~TransportSocketParams();

  HostResolver::RequestInfo destination_;
  const OnHostResolutionCallback host_resolution_callback_;
  CombineConnectAndWritePolicy combine_connect_and_write_;

  DISALLOW_COPY_AND_ASSIGN(TransportSocketParams);
};

// TransportConnectJob handles the host resolution necessary for socket creation
// and the transport (likely TCP) connect. TransportConnectJob also has fallback
// logic for IPv6 connect() timeouts (which may happen due to networks / routers
// with broken IPv6 support). Those timeouts take 20s, so rather than make the
// user wait 20s for the timeout to fire, we use a fallback timer
// (kIPv6FallbackTimerInMs) and start a connect() to a IPv4 address if the timer
// fires. Then we race the IPv4 connect() against the IPv6 connect() (which has
// a headstart) and return the one that completes first to the socket pool.
class NET_EXPORT_PRIVATE TransportConnectJob : public ConnectJob {
 public:
  // For recording the connection time in the appropriate bucket.
  enum RaceResult {
    RACE_UNKNOWN,
    RACE_IPV4_WINS,
    RACE_IPV4_SOLO,
    RACE_IPV6_WINS,
    RACE_IPV6_SOLO,
  };

  // TransportConnectJobs will time out after this many seconds.  Note this is
  // the total time, including both host resolution and TCP connect() times.
  static const int kTimeoutInSeconds;

  // In cases where both IPv6 and IPv4 addresses were returned from DNS,
  // TransportConnectJobs will start a second connection attempt to just the
  // IPv4 addresses after this many milliseconds. (This is "Happy Eyeballs".)
  static const int kIPv6FallbackTimerInMs;

  TransportConnectJob(
      const std::string& group_name,
      RequestPriority priority,
      const SocketTag& socket_tag,
      ClientSocketPool::RespectLimits respect_limits,
      const scoped_refptr<TransportSocketParams>& params,
      base::TimeDelta timeout_duration,
      ClientSocketFactory* client_socket_factory,
      SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
      HostResolver* host_resolver,
      Delegate* delegate,
      NetLog* net_log);
  ~TransportConnectJob() override;

  // ConnectJob methods.
  LoadState GetLoadState() const override;
  void GetAdditionalErrorState(ClientSocketHandle* handle) override;

  // Rolls |addrlist| forward until the first IPv4 address, if any.
  // WARNING: this method should only be used to implement the prefer-IPv4 hack.
  static void MakeAddressListStartWithIPv4(AddressList* addrlist);

  // Record the histograms Net.DNS_Resolution_And_TCP_Connection_Latency2 and
  // Net.TCP_Connection_Latency and return the connect duration.
  static void HistogramDuration(
      const LoadTimingInfo::ConnectTiming& connect_timing,
      RaceResult race_result);

 private:
  enum State {
    STATE_RESOLVE_HOST,
    STATE_RESOLVE_HOST_COMPLETE,
    STATE_TRANSPORT_CONNECT,
    STATE_TRANSPORT_CONNECT_COMPLETE,
    STATE_NONE,
  };

  void OnIOComplete(int result);
  int DoLoop(int result);

  int DoResolveHost();
  int DoResolveHostComplete(int result);
  int DoTransportConnect();
  int DoTransportConnectComplete(int result);

  // Not part of the state machine.
  void DoIPv6FallbackTransportConnect();
  void DoIPv6FallbackTransportConnectComplete(int result);

  // Begins the host resolution and the TCP connect.  Returns OK on success
  // and ERR_IO_PENDING if it cannot immediately service the request.
  // Otherwise, it returns a net error code.
  int ConnectInternal() override;

  void CopyConnectionAttemptsFromSockets();

  scoped_refptr<TransportSocketParams> params_;
  HostResolver* resolver_;
  std::unique_ptr<HostResolver::Request> request_;
  ClientSocketFactory* const client_socket_factory_;

  State next_state_;

  std::unique_ptr<StreamSocket> transport_socket_;
  AddressList addresses_;

  std::unique_ptr<StreamSocket> fallback_transport_socket_;
  std::unique_ptr<AddressList> fallback_addresses_;
  base::TimeTicks fallback_connect_start_time_;
  base::OneShotTimer fallback_timer_;
  SocketPerformanceWatcherFactory* socket_performance_watcher_factory_;

  int resolve_result_;

  // Used in the failure case to save connection attempts made on the main and
  // fallback sockets and pass them on in |GetAdditionalErrorState|. (In the
  // success case, connection attempts are passed through the returned socket;
  // attempts are copied from the other socket, if one exists, into it before
  // it is returned.)
  ConnectionAttempts connection_attempts_;
  ConnectionAttempts fallback_connection_attempts_;

  DISALLOW_COPY_AND_ASSIGN(TransportConnectJob);
};

class NET_EXPORT_PRIVATE TransportClientSocketPool : public ClientSocketPool {
 public:
  typedef TransportSocketParams SocketParams;

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
  base::TimeDelta ConnectionTimeout() const override;

  // HigherLayeredPool implementation.
  bool IsStalled() const override;
  void AddHigherLayeredPool(HigherLayeredPool* higher_pool) override;
  void RemoveHigherLayeredPool(HigherLayeredPool* higher_pool) override;

  ClientSocketFactory* client_socket_factory() {
    return client_socket_factory_;
  }

 protected:
  // Methods shared with WebSocketTransportClientSocketPool
  void NetLogTcpClientSocketPoolRequestedSocket(
      const NetLogWithSource& net_log,
      const scoped_refptr<TransportSocketParams>* casted_params);

 private:
  typedef ClientSocketPoolBase<TransportSocketParams> PoolBase;

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

    base::TimeDelta ConnectionTimeout() const override;

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
