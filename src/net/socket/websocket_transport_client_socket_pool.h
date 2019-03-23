// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_WEBSOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_
#define NET_SOCKET_WEBSOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/net_export.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_pool.h"
#include "net/socket/client_socket_pool_base.h"
#include "net/socket/transport_client_socket_pool.h"

namespace base {
class DictionaryValue;
}

namespace net {

class ClientSocketFactory;
class HostResolver;
class NetLog;
class WebSocketEndpointLockManager;
class WebSocketTransportConnectJob;

class NET_EXPORT_PRIVATE WebSocketTransportClientSocketPool
    : public TransportClientSocketPool {
 public:
  WebSocketTransportClientSocketPool(
      int max_sockets,
      int max_sockets_per_group,
      HostResolver* host_resolver,
      ClientSocketFactory* client_socket_factory,
      WebSocketEndpointLockManager* websocket_endpoint_lock_manager,
      NetLog* net_log);

  ~WebSocketTransportClientSocketPool() override;

  // Allow another connection to be started to the IPEndPoint that this |handle|
  // is connected to. Used when the WebSocket handshake completes successfully.
  // This only works if the socket is connected, however the caller does not
  // need to explicitly check for this. Instead, ensure that dead sockets are
  // returned to ReleaseSocket() in a timely fashion.
  static void UnlockEndpoint(
      ClientSocketHandle* handle,
      WebSocketEndpointLockManager* websocket_endpoint_lock_manager);

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

  // HigherLayeredPool implementation.
  bool IsStalled() const override;

 private:
  class ConnectJobDelegate : public ConnectJob::Delegate {
   public:
    ConnectJobDelegate(WebSocketTransportClientSocketPool* owner,
                       CompletionOnceCallback callback,
                       ClientSocketHandle* socket_handle,
                       const NetLogWithSource& request_net_log);
    ~ConnectJobDelegate() override;

    // ConnectJob::Delegate implementation
    void OnConnectJobComplete(int result, ConnectJob* job) override;

    // Calls Connect() on |connect_job|, and takes ownership. Returns Connect's
    // return value.
    int Connect(std::unique_ptr<ConnectJob> connect_job);

    CompletionOnceCallback release_callback() { return std::move(callback_); }
    ConnectJob* connect_job() { return connect_job_.get(); }
    ClientSocketHandle* socket_handle() { return socket_handle_; }

    const NetLogWithSource& request_net_log() { return request_net_log_; }
    const NetLogWithSource& connect_job_net_log();

   private:
    WebSocketTransportClientSocketPool* owner_;

    CompletionOnceCallback callback_;
    std::unique_ptr<ConnectJob> connect_job_;
    ClientSocketHandle* const socket_handle_;
    const NetLogWithSource request_net_log_;

    DISALLOW_COPY_AND_ASSIGN(ConnectJobDelegate);
  };

  // Store the arguments from a call to RequestSocket() that has stalled so we
  // can replay it when there are available socket slots.
  struct StalledRequest {
    StalledRequest(const scoped_refptr<SocketParams>& params,
                   RequestPriority priority,
                   ClientSocketHandle* handle,
                   CompletionOnceCallback callback,
                   const NetLogWithSource& net_log);
    StalledRequest(StalledRequest&& other);
    ~StalledRequest();

    const scoped_refptr<SocketParams> params;
    const RequestPriority priority;
    ClientSocketHandle* const handle;
    CompletionOnceCallback callback;
    const NetLogWithSource net_log;
  };

  typedef std::map<const ClientSocketHandle*,
                   std::unique_ptr<ConnectJobDelegate>>
      PendingConnectsMap;
  // This is a list so that we can remove requests from the middle, and also
  // so that iterators are not invalidated unless the corresponding request is
  // removed.
  typedef std::list<StalledRequest> StalledRequestQueue;
  typedef std::map<const ClientSocketHandle*, StalledRequestQueue::iterator>
      StalledRequestMap;

  // Tries to hand out the socket connected by |job|. |result| must be (async)
  // result of WebSocketTransportConnectJob::Connect(). Returns true iff it has
  // handed out a socket.
  bool TryHandOutSocket(int result, ConnectJobDelegate* connect_job_delegate);
  void OnConnectJobComplete(int result,
                            ConnectJobDelegate* connect_job_delegate);
  void InvokeUserCallbackLater(ClientSocketHandle* handle,
                               CompletionOnceCallback callback,
                               int rv);
  void InvokeUserCallback(ClientSocketHandle* handle,
                          CompletionOnceCallback callback,
                          int rv);
  bool ReachedMaxSocketsLimit() const;
  void HandOutSocket(std::unique_ptr<StreamSocket> socket,
                     const LoadTimingInfo::ConnectTiming& connect_timing,
                     ClientSocketHandle* handle,
                     const NetLogWithSource& net_log);
  void AddJob(ClientSocketHandle* handle,
              std::unique_ptr<ConnectJobDelegate> delegate);
  bool DeleteJob(ClientSocketHandle* handle);
  const ConnectJob* LookupConnectJob(const ClientSocketHandle* handle) const;
  void ActivateStalledRequest();
  bool DeleteStalledRequest(ClientSocketHandle* handle);

  std::set<const ClientSocketHandle*> pending_callbacks_;
  PendingConnectsMap pending_connects_;
  StalledRequestQueue stalled_request_queue_;
  StalledRequestMap stalled_request_map_;
  NetLog* const pool_net_log_;
  ClientSocketFactory* const client_socket_factory_;
  HostResolver* const host_resolver_;
  WebSocketEndpointLockManager* websocket_endpoint_lock_manager_;
  const int max_sockets_;
  int handed_out_socket_count_;
  bool flushing_;

  base::WeakPtrFactory<WebSocketTransportClientSocketPool> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(WebSocketTransportClientSocketPool);
};

}  // namespace net

#endif  // NET_SOCKET_WEBSOCKET_TRANSPORT_CLIENT_SOCKET_POOL_H_
