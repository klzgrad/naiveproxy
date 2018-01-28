// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/client_socket_handle.h"

#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/trace_event/trace_event.h"
#include "net/base/net_errors.h"
#include "net/base/trace_constants.h"
#include "net/log/net_log_event_type.h"
#include "net/socket/client_socket_pool.h"

namespace net {

ClientSocketHandle::ClientSocketHandle()
    : is_initialized_(false),
      pool_(NULL),
      higher_pool_(NULL),
      reuse_type_(ClientSocketHandle::UNUSED),
      callback_(base::Bind(&ClientSocketHandle::OnIOComplete,
                           base::Unretained(this))),
      is_ssl_error_(false) {}

ClientSocketHandle::~ClientSocketHandle() {
  Reset();
}

void ClientSocketHandle::SetPriority(RequestPriority priority) {
  if (socket_) {
    // The priority of the handle is no longer relevant to the socket pool;
    // just return.
    return;
  }

  if (pool_)
    pool_->SetPriority(group_name_, this, priority);
}

void ClientSocketHandle::Reset() {
  ResetInternal(true);
  ResetErrorState();
}

void ClientSocketHandle::ResetInternal(bool cancel) {
  // Was Init called?
  if (!group_name_.empty()) {
    // If so, we must have a pool.
    CHECK(pool_);
    if (is_initialized()) {
      if (socket_) {
        socket_->NetLog().EndEvent(NetLogEventType::SOCKET_IN_USE);
        // Release the socket back to the ClientSocketPool so it can be
        // deleted or reused.
        pool_->ReleaseSocket(group_name_, std::move(socket_), pool_id_);
      } else {
        // If the handle has been initialized, we should still have a
        // socket.
        NOTREACHED();
      }
    } else if (cancel) {
      // If we did not get initialized yet and we have a socket
      // request pending, cancel it.
      pool_->CancelRequest(group_name_, this);
    }
  }
  is_initialized_ = false;
  socket_.reset();
  group_name_.clear();
  reuse_type_ = ClientSocketHandle::UNUSED;
  user_callback_.Reset();
  if (higher_pool_)
    RemoveHigherLayeredPool(higher_pool_);
  pool_ = NULL;
  idle_time_ = base::TimeDelta();
  connect_timing_ = LoadTimingInfo::ConnectTiming();
  pool_id_ = -1;
}

void ClientSocketHandle::ResetErrorState() {
  is_ssl_error_ = false;
  ssl_error_response_info_ = HttpResponseInfo();
  pending_http_proxy_connection_.reset();
}

LoadState ClientSocketHandle::GetLoadState() const {
  CHECK(!is_initialized());
  CHECK(!group_name_.empty());
  // Because of http://crbug.com/37810  we may not have a pool, but have
  // just a raw socket.
  if (!pool_)
    return LOAD_STATE_IDLE;
  return pool_->GetLoadState(group_name_, this);
}

bool ClientSocketHandle::IsPoolStalled() const {
  if (!pool_)
    return false;
  return pool_->IsStalled();
}

void ClientSocketHandle::AddHigherLayeredPool(HigherLayeredPool* higher_pool) {
  CHECK(higher_pool);
  CHECK(!higher_pool_);
  // TODO(mmenke):  |pool_| should only be NULL in tests.  Maybe stop doing that
  // so this be be made into a DCHECK, and the same can be done in
  // RemoveHigherLayeredPool?
  if (pool_) {
    pool_->AddHigherLayeredPool(higher_pool);
    higher_pool_ = higher_pool;
  }
}

void ClientSocketHandle::RemoveHigherLayeredPool(
    HigherLayeredPool* higher_pool) {
  CHECK(higher_pool_);
  CHECK_EQ(higher_pool_, higher_pool);
  if (pool_) {
    pool_->RemoveHigherLayeredPool(higher_pool);
    higher_pool_ = NULL;
  }
}

void ClientSocketHandle::CloseIdleSocketsInGroup() {
  if (pool_)
    pool_->CloseIdleSocketsInGroup(group_name_);
}

bool ClientSocketHandle::GetLoadTimingInfo(
    bool is_reused,
    LoadTimingInfo* load_timing_info) const {
  // Only return load timing information when there's a socket.
  if (!socket_)
    return false;

  load_timing_info->socket_log_id = socket_->NetLog().source().id;
  load_timing_info->socket_reused = is_reused;

  // No times if the socket is reused.
  if (is_reused)
    return true;

  load_timing_info->connect_timing = connect_timing_;
  return true;
}

void ClientSocketHandle::DumpMemoryStats(
    StreamSocket::SocketMemoryStats* stats) const {
  if (!socket_)
    return;
  socket_->DumpMemoryStats(stats);
}

void ClientSocketHandle::SetSocket(std::unique_ptr<StreamSocket> s) {
  socket_ = std::move(s);
}

void ClientSocketHandle::OnIOComplete(int result) {
  TRACE_EVENT0(kNetTracingCategory, "ClientSocketHandle::OnIOComplete");
  CompletionCallback callback = user_callback_;
  user_callback_.Reset();
  HandleInitCompletion(result);
  callback.Run(result);
}

std::unique_ptr<StreamSocket> ClientSocketHandle::PassSocket() {
  return std::move(socket_);
}

void ClientSocketHandle::HandleInitCompletion(int result) {
  CHECK_NE(ERR_IO_PENDING, result);
  if (result != OK) {
    if (!socket_.get())
      ResetInternal(false);  // Nothing to cancel since the request failed.
    else
      is_initialized_ = true;
    return;
  }
  is_initialized_ = true;
  CHECK_NE(-1, pool_id_) << "Pool should have set |pool_id_| to a valid value.";

  // Broadcast that the socket has been acquired.
  // TODO(eroman): This logging is not complete, in particular set_socket() and
  // release() socket. It ends up working though, since those methods are being
  // used to layer sockets (and the destination sources are the same).
  DCHECK(socket_.get());
  socket_->NetLog().BeginEvent(NetLogEventType::SOCKET_IN_USE,
                               requesting_source_.ToEventParametersCallback());
}

}  // namespace net
