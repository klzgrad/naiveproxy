// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socks_connect_job.h"

#include <utility>

#include "base/bind.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_source_type.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/socks5_client_socket.h"
#include "net/socket/socks_client_socket.h"
#include "net/socket/transport_connect_job.h"

namespace net {

// SOCKSConnectJobs will time out after this many seconds.  Note this is in
// addition to the timeout for the transport socket.
static const int kSOCKSConnectJobTimeoutInSeconds = 30;

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

SOCKSConnectJob::SOCKSConnectJob(
    RequestPriority priority,
    const CommonConnectJobParams& common_connect_job_params,
    const scoped_refptr<SOCKSSocketParams>& socks_params,
    ConnectJob::Delegate* delegate,
    const NetLogWithSource* net_log)
    : ConnectJob(priority,
                 ConnectionTimeout(),
                 common_connect_job_params,
                 delegate,
                 net_log,
                 NetLogSourceType::SOCKS_CONNECT_JOB,
                 NetLogEventType::SOCKS_CONNECT_JOB_CONNECT),
      socks_params_(socks_params) {}

SOCKSConnectJob::~SOCKSConnectJob() {
  // In the case the job was canceled, need to delete nested job first to
  // correctly order NetLog events.
  transport_connect_job_.reset();
}

LoadState SOCKSConnectJob::GetLoadState() const {
  switch (next_state_) {
    case STATE_TRANSPORT_CONNECT:
      return LOAD_STATE_IDLE;
    case STATE_TRANSPORT_CONNECT_COMPLETE:
      return transport_connect_job_->GetLoadState();
    case STATE_SOCKS_CONNECT:
    case STATE_SOCKS_CONNECT_COMPLETE:
      return LOAD_STATE_CONNECTING;
    default:
      NOTREACHED();
      return LOAD_STATE_IDLE;
  }
}

bool SOCKSConnectJob::HasEstablishedConnection() const {
  return next_state_ == STATE_SOCKS_CONNECT ||
         next_state_ == STATE_SOCKS_CONNECT_COMPLETE;
}

base::TimeDelta SOCKSConnectJob::ConnectionTimeout() {
  return TransportConnectJob::ConnectionTimeout() +
         base::TimeDelta::FromSeconds(kSOCKSConnectJobTimeoutInSeconds);
}

void SOCKSConnectJob::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    NotifyDelegateOfCompletion(rv);  // Deletes |this|
}

void SOCKSConnectJob::OnConnectJobComplete(int result, ConnectJob* job) {
  DCHECK(transport_connect_job_);
  DCHECK_EQ(next_state_, STATE_TRANSPORT_CONNECT_COMPLETE);
  OnIOComplete(result);
}

void SOCKSConnectJob::OnNeedsProxyAuth(
    const HttpResponseInfo& response,
    HttpAuthController* auth_controller,
    base::OnceClosure restart_with_auth_callback,
    ConnectJob* job) {
  // A SOCKSConnectJob can't be on top of an HttpProxyConnectJob.
  NOTREACHED();
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
  DCHECK(!transport_connect_job_);

  next_state_ = STATE_TRANSPORT_CONNECT_COMPLETE;
  transport_connect_job_ = TransportConnectJob::CreateTransportConnectJob(
      socks_params_->transport_params(), priority(),
      common_connect_job_params(), this, &net_log());
  return transport_connect_job_->Connect();
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
    socket_.reset(new SOCKS5ClientSocket(transport_connect_job_->PassSocket(),
                                         socks_params_->destination(),
                                         socks_params_->traffic_annotation()));
  } else {
    socket_.reset(new SOCKSClientSocket(
        transport_connect_job_->PassSocket(), socks_params_->destination(),
        priority(), host_resolver(), socks_params_->traffic_annotation()));
  }
  transport_connect_job_.reset();
  return socket_->Connect(
      base::BindOnce(&SOCKSConnectJob::OnIOComplete, base::Unretained(this)));
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
  if (transport_connect_job_)
    transport_connect_job_->ChangePriority(priority);
}

}  // namespace net
