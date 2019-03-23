// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/connect_job.h"

#include "base/trace_event/trace_event.h"
#include "net/base/net_errors.h"
#include "net/base/trace_constants.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/stream_socket.h"

namespace net {

CommonConnectJobParams::CommonConnectJobParams(
    const std::string& group_name,
    const SocketTag& socket_tag,
    bool respect_limits,
    ClientSocketFactory* client_socket_factory,
    SocketPerformanceWatcherFactory* socket_performance_watcher_factory,
    HostResolver* host_resolver,
    NetLog* net_log,
    WebSocketEndpointLockManager* websocket_endpoint_lock_manager)
    : group_name(group_name),
      socket_tag(socket_tag),
      respect_limits(respect_limits),
      client_socket_factory(client_socket_factory),
      socket_performance_watcher_factory(socket_performance_watcher_factory),
      host_resolver(host_resolver),
      net_log(net_log),
      websocket_endpoint_lock_manager(websocket_endpoint_lock_manager) {
  DCHECK(!group_name.empty());
}

CommonConnectJobParams::CommonConnectJobParams(
    const CommonConnectJobParams& other) = default;

CommonConnectJobParams::~CommonConnectJobParams() = default;

CommonConnectJobParams& CommonConnectJobParams::operator=(
    const CommonConnectJobParams& other) = default;

ConnectJob::ConnectJob(RequestPriority priority,
                       base::TimeDelta timeout_duration,
                       const CommonConnectJobParams& common_connect_job_params,
                       Delegate* delegate,
                       const NetLogWithSource& net_log)
    : timeout_duration_(timeout_duration),
      priority_(priority),
      common_connect_job_params_(common_connect_job_params),
      delegate_(delegate),
      net_log_(net_log) {
  DCHECK(delegate);
  net_log.BeginEvent(NetLogEventType::SOCKET_POOL_CONNECT_JOB,
                     NetLog::StringCallback(
                         "group_name", &common_connect_job_params.group_name));
}

ConnectJob::ConnectJob(const std::string& group_name,
                       base::TimeDelta timeout_duration,
                       RequestPriority priority,
                       const SocketTag& socket_tag,
                       bool respect_limits,
                       Delegate* delegate,
                       const NetLogWithSource& net_log)
    : ConnectJob(priority,
                 timeout_duration,
                 CommonConnectJobParams(
                     group_name,
                     socket_tag,
                     respect_limits,
                     nullptr /* client_socket_factory */,
                     nullptr /* socket_performance_watcher_factory */,
                     nullptr /* host_resolver */,
                     nullptr /* net_log */,
                     nullptr /* websocket_endpoint_lock_manager */),
                 delegate,
                 net_log) {}

ConnectJob::~ConnectJob() {
  net_log().EndEvent(NetLogEventType::SOCKET_POOL_CONNECT_JOB);
}

std::unique_ptr<StreamSocket> ConnectJob::PassSocket() {
  return std::move(socket_);
}

void ConnectJob::ChangePriority(RequestPriority priority) {
  // Priority of a job that ignores limits should not be changed because it
  // should always be MAXIMUM_PRIORITY.
  DCHECK(respect_limits());
  priority_ = priority;
  ChangePriorityInternal(priority);
}

int ConnectJob::Connect() {
  if (!timeout_duration_.is_zero())
    timer_.Start(FROM_HERE, timeout_duration_, this, &ConnectJob::OnTimeout);

  LogConnectStart();

  int rv = ConnectInternal();

  if (rv != ERR_IO_PENDING) {
    LogConnectCompletion(rv);
    delegate_ = nullptr;
  }

  return rv;
}

void ConnectJob::SetSocket(std::unique_ptr<StreamSocket> socket) {
  if (socket) {
    net_log().AddEvent(NetLogEventType::CONNECT_JOB_SET_SOCKET,
                       socket->NetLog().source().ToEventParametersCallback());
  }
  socket_ = std::move(socket);
}

void ConnectJob::NotifyDelegateOfCompletion(int rv) {
  TRACE_EVENT0(NetTracingCategory(), "ConnectJob::NotifyDelegateOfCompletion");
  // The delegate will own |this|.
  Delegate* delegate = delegate_;
  delegate_ = nullptr;

  LogConnectCompletion(rv);
  delegate->OnConnectJobComplete(rv, this);
}

void ConnectJob::ResetTimer(base::TimeDelta remaining_time) {
  timer_.Stop();
  timer_.Start(FROM_HERE, remaining_time, this, &ConnectJob::OnTimeout);
}

void ConnectJob::LogConnectStart() {
  connect_timing_.connect_start = base::TimeTicks::Now();
  net_log().BeginEvent(NetLogEventType::SOCKET_POOL_CONNECT_JOB_CONNECT);
}

void ConnectJob::LogConnectCompletion(int net_error) {
  connect_timing_.connect_end = base::TimeTicks::Now();
  net_log().EndEventWithNetErrorCode(
      NetLogEventType::SOCKET_POOL_CONNECT_JOB_CONNECT, net_error);
}

void ConnectJob::OnTimeout() {
  // Make sure the socket is NULL before calling into |delegate|.
  SetSocket(nullptr);

  net_log_.AddEvent(NetLogEventType::SOCKET_POOL_CONNECT_JOB_TIMED_OUT);

  NotifyDelegateOfCompletion(ERR_TIMED_OUT);
}

}  // namespace net
