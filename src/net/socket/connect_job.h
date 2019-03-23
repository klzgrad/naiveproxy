// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SOCKET_CONNECT_JOB_H_
#define NET_SOCKET_CONNECT_JOB_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/load_states.h"
#include "net/base/load_timing_info.h"
#include "net/base/net_export.h"
#include "net/base/request_priority.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/socket_tag.h"

namespace net {

class ClientSocketFactory;
class ClientSocketHandle;
class HostResolver;
class NetLog;
class SocketPerformanceWatcherFactory;
class StreamSocket;
class WebSocketEndpointLockManager;

// Immutable socket parameters intended for shared use by all ConnectJob types.
// Excludes priority because it can be modified over the lifetime of a
// ConnectJob. Excludes connection timeout and NetLogWithSource because
// ConnectJobs that wrap other ConnectJobs typically have different values for
// those.
struct NET_EXPORT_PRIVATE CommonConnectJobParams {
  CommonConnectJobParams(
      const std::string& group_name,
      const SocketTag& socket_tag,
      bool respect_limits,
      ClientSocketFactory* client_socket_factory,
      SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
      HostResolver* host_resolver,
      NetLog* net_log,
      WebSocketEndpointLockManager* websocket_endpoint_lock_manager);
  CommonConnectJobParams(const CommonConnectJobParams& other);
  ~CommonConnectJobParams();

  CommonConnectJobParams& operator=(const CommonConnectJobParams& other);

  // Socket pool group name, used for logging and identying the group in a
  // socket pool.
  // TODO(mmenke): Remove the latter use.
  std::string group_name;

  // Tag applied to any created socket.
  SocketTag socket_tag;

  // Whether connection limits should be respected.
  // TODO(mmenke): Look into removing this. Only needed here because socket
  // pools query it.
  bool respect_limits;

  ClientSocketFactory* client_socket_factory;
  SocketPerformanceWatcherFactory* socket_performance_watcher_factory;
  HostResolver* host_resolver;
  NetLog* net_log;

  // This must only be non-null for WebSockets.
  WebSocketEndpointLockManager* websocket_endpoint_lock_manager;
};

// ConnectJob provides an abstract interface for "connecting" a socket.
// The connection may involve host resolution, tcp connection, ssl connection,
// etc.
class NET_EXPORT_PRIVATE ConnectJob {
 public:
  // Alerts the delegate that the connection completed. |job| must be destroyed
  // by the delegate. A std::unique_ptr<> isn't used because the caller of this
  // function doesn't own |job|.
  class NET_EXPORT_PRIVATE Delegate {
   public:
    Delegate() {}
    virtual ~Delegate() {}

    // Alerts the delegate that the connection completed. |job| must be
    // destroyed by the delegate. A std::unique_ptr<> isn't used because the
    // caller of this function doesn't own |job|.
    virtual void OnConnectJobComplete(int result, ConnectJob* job) = 0;

   private:
    DISALLOW_COPY_AND_ASSIGN(Delegate);
  };

  // A |timeout_duration| of 0 corresponds to no timeout. |group_name| is a
  // caller-provided opaque string, only used for logging and the corresponding
  // accessor.
  ConnectJob(RequestPriority priority,
             base::TimeDelta timeout_duration,
             const CommonConnectJobParams& common_connect_job_params,
             Delegate* delegate,
             const NetLogWithSource& net_log);
  // Legacy constructor that takes all pointers in CommonSocketParams as
  // nullptr.
  // TODO(mmenke): Remove this.
  ConnectJob(const std::string& group_name,
             base::TimeDelta timeout_duration,
             RequestPriority priority,
             const SocketTag& socket_tag,
             bool respect_limits,
             Delegate* delegate,
             const NetLogWithSource& net_log);
  virtual ~ConnectJob();

  // Accessors
  const std::string& group_name() const {
    return common_connect_job_params_.group_name;
  }
  const NetLogWithSource& net_log() { return net_log_; }
  RequestPriority priority() const { return priority_; }
  bool respect_limits() const {
    return common_connect_job_params_.respect_limits;
  }

  // Releases ownership of the underlying socket to the caller. Returns the
  // released socket, or nullptr if there was a connection error.
  std::unique_ptr<StreamSocket> PassSocket();

  void ChangePriority(RequestPriority priority);

  // Begins connecting the socket.  Returns OK on success, ERR_IO_PENDING if it
  // cannot complete synchronously without blocking, or another net error code
  // on error.  In asynchronous completion, the ConnectJob will notify
  // |delegate_| via OnConnectJobComplete.  In both asynchronous and synchronous
  // completion, ReleaseSocket() can be called to acquire the connected socket
  // if it succeeded.
  //
  // On completion, the ConnectJob must be completed synchronously, since it
  // doesn't bother to stop its timer when complete.
  int Connect();

  virtual LoadState GetLoadState() const = 0;

  // If Connect returns an error (or OnConnectJobComplete reports an error
  // result) this method will be called, allowing a SocketPool to add additional
  // error state to the ClientSocketHandle (post late-binding).
  //
  // TODO(mmenke): This is a layering violation. Consider refactoring it to not
  // depend on ClientSocketHandle. Fixing this will need to wait until after
  // proxy tunnel auth has been refactored.
  virtual void GetAdditionalErrorState(ClientSocketHandle* handle) {}

  const LoadTimingInfo::ConnectTiming& connect_timing() const {
    return connect_timing_;
  }

  const NetLogWithSource& net_log() const { return net_log_; }

 protected:
  const SocketTag& socket_tag() const {
    return common_connect_job_params_.socket_tag;
  }
  ClientSocketFactory* client_socket_factory() {
    return common_connect_job_params_.client_socket_factory;
  }
  SocketPerformanceWatcherFactory* socket_performance_watcher_factory() {
    return common_connect_job_params_.socket_performance_watcher_factory;
  }
  HostResolver* host_resolver() {
    return common_connect_job_params_.host_resolver;
  }
  WebSocketEndpointLockManager* websocket_endpoint_lock_manager() {
    return common_connect_job_params_.websocket_endpoint_lock_manager;
  }
  const CommonConnectJobParams& common_connect_job_params() {
    return common_connect_job_params_;
  }

  void SetSocket(std::unique_ptr<StreamSocket> socket);
  StreamSocket* socket() { return socket_.get(); }
  void NotifyDelegateOfCompletion(int rv);
  void ResetTimer(base::TimeDelta remaining_time);

  // Connection establishment timing information.
  // TODO(mmenke): This should be private.
  LoadTimingInfo::ConnectTiming connect_timing_;

 private:
  virtual int ConnectInternal() = 0;

  virtual void ChangePriorityInternal(RequestPriority priority) = 0;

  void LogConnectStart();
  void LogConnectCompletion(int net_error);

  // Alerts the delegate that the ConnectJob has timed out.
  void OnTimeout();

  const base::TimeDelta timeout_duration_;
  RequestPriority priority_;
  const CommonConnectJobParams common_connect_job_params_;
  // Timer to abort jobs that take too long.
  base::OneShotTimer timer_;
  Delegate* delegate_;
  std::unique_ptr<StreamSocket> socket_;
  NetLogWithSource net_log_;

  DISALLOW_COPY_AND_ASSIGN(ConnectJob);
};

}  // namespace net

#endif  // NET_SOCKET_CONNECT_JOB_H_
