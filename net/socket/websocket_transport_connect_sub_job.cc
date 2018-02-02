// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/websocket_transport_connect_sub_job.h"

#include "base/logging.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/client_socket_factory.h"
#include "net/socket/websocket_endpoint_lock_manager.h"

namespace net {

WebSocketTransportConnectSubJob::WebSocketTransportConnectSubJob(
    const AddressList& addresses,
    WebSocketTransportConnectJob* parent_job,
    SubJobType type)
    : parent_job_(parent_job),
      addresses_(addresses),
      current_address_index_(0),
      next_state_(STATE_NONE),
      type_(type) {}

WebSocketTransportConnectSubJob::~WebSocketTransportConnectSubJob() {
  // We don't worry about cancelling the TCP connect, since ~StreamSocket will
  // take care of it.
  if (next()) {
    DCHECK_EQ(STATE_OBTAIN_LOCK_COMPLETE, next_state_);
    // The ~Waiter destructor will remove this object from the waiting list.
  } else if (next_state_ == STATE_TRANSPORT_CONNECT_COMPLETE) {
    WebSocketEndpointLockManager::GetInstance()->UnlockEndpoint(
        CurrentAddress());
  }
}

// Start connecting.
int WebSocketTransportConnectSubJob::Start() {
  DCHECK_EQ(STATE_NONE, next_state_);
  next_state_ = STATE_OBTAIN_LOCK;
  return DoLoop(OK);
}

// Called by WebSocketEndpointLockManager when the lock becomes available.
void WebSocketTransportConnectSubJob::GotEndpointLock() {
  DCHECK_EQ(STATE_OBTAIN_LOCK_COMPLETE, next_state_);
  OnIOComplete(OK);
}

LoadState WebSocketTransportConnectSubJob::GetLoadState() const {
  switch (next_state_) {
    case STATE_OBTAIN_LOCK:
    case STATE_OBTAIN_LOCK_COMPLETE:
      // TODO(ricea): Add a WebSocket-specific LOAD_STATE ?
      return LOAD_STATE_WAITING_FOR_AVAILABLE_SOCKET;
    case STATE_TRANSPORT_CONNECT:
    case STATE_TRANSPORT_CONNECT_COMPLETE:
    case STATE_DONE:
      return LOAD_STATE_CONNECTING;
    case STATE_NONE:
      return LOAD_STATE_IDLE;
  }
  NOTREACHED();
  return LOAD_STATE_IDLE;
}

ClientSocketFactory* WebSocketTransportConnectSubJob::client_socket_factory()
    const {
  return parent_job_->client_socket_factory_;
}

const NetLogWithSource& WebSocketTransportConnectSubJob::net_log() const {
  return parent_job_->net_log();
}

const IPEndPoint& WebSocketTransportConnectSubJob::CurrentAddress() const {
  DCHECK_LT(current_address_index_, addresses_.size());
  return addresses_[current_address_index_];
}

void WebSocketTransportConnectSubJob::OnIOComplete(int result) {
  int rv = DoLoop(result);
  if (rv != ERR_IO_PENDING)
    parent_job_->OnSubJobComplete(rv, this);  // |this| deleted
}

int WebSocketTransportConnectSubJob::DoLoop(int result) {
  DCHECK_NE(next_state_, STATE_NONE);

  int rv = result;
  do {
    State state = next_state_;
    next_state_ = STATE_NONE;
    switch (state) {
      case STATE_OBTAIN_LOCK:
        DCHECK_EQ(OK, rv);
        rv = DoEndpointLock();
        break;
      case STATE_OBTAIN_LOCK_COMPLETE:
        DCHECK_EQ(OK, rv);
        rv = DoEndpointLockComplete();
        break;
      case STATE_TRANSPORT_CONNECT:
        DCHECK_EQ(OK, rv);
        rv = DoTransportConnect();
        break;
      case STATE_TRANSPORT_CONNECT_COMPLETE:
        rv = DoTransportConnectComplete(rv);
        break;
      default:
        NOTREACHED();
        rv = ERR_FAILED;
        break;
    }
  } while (rv != ERR_IO_PENDING && next_state_ != STATE_NONE &&
           next_state_ != STATE_DONE);

  return rv;
}

int WebSocketTransportConnectSubJob::DoEndpointLock() {
  int rv = WebSocketEndpointLockManager::GetInstance()->LockEndpoint(
      CurrentAddress(), this);
  next_state_ = STATE_OBTAIN_LOCK_COMPLETE;
  return rv;
}

int WebSocketTransportConnectSubJob::DoEndpointLockComplete() {
  next_state_ = STATE_TRANSPORT_CONNECT;
  return OK;
}

int WebSocketTransportConnectSubJob::DoTransportConnect() {
  // TODO(ricea): Update global g_last_connect_time and report
  // ConnectInterval.
  next_state_ = STATE_TRANSPORT_CONNECT_COMPLETE;
  AddressList one_address(CurrentAddress());
  transport_socket_ = client_socket_factory()->CreateTransportClientSocket(
      one_address, nullptr, net_log().net_log(), net_log().source());
  // This use of base::Unretained() is safe because transport_socket_ is
  // destroyed in the destructor.
  return transport_socket_->Connect(base::Bind(
      &WebSocketTransportConnectSubJob::OnIOComplete, base::Unretained(this)));
}

int WebSocketTransportConnectSubJob::DoTransportConnectComplete(int result) {
  next_state_ = STATE_DONE;
  WebSocketEndpointLockManager* endpoint_lock_manager =
      WebSocketEndpointLockManager::GetInstance();
  if (result != OK) {
    endpoint_lock_manager->UnlockEndpoint(CurrentAddress());

    if (current_address_index_ + 1 < addresses_.size()) {
      // Try falling back to the next address in the list.
      next_state_ = STATE_OBTAIN_LOCK;
      ++current_address_index_;
      result = OK;
    }

    return result;
  }

  endpoint_lock_manager->RememberSocket(transport_socket_.get(),
                                        CurrentAddress());

  return result;
}

}  // namespace net
