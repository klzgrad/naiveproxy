// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/dns_session.h"

#include <stdint.h>

#include <limits>
#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "base/metrics/histogram_macros.h"
#include "base/rand_util.h"
#include "base/stl_util.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/dns/dns_config.h"
#include "net/dns/dns_socket_pool.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_source.h"
#include "net/log/net_log_with_source.h"
#include "net/socket/datagram_client_socket.h"
#include "net/socket/stream_socket.h"

namespace net {

DnsSession::SocketLease::SocketLease(
    scoped_refptr<DnsSession> session,
    size_t server_index,
    std::unique_ptr<DatagramClientSocket> socket)
    : session_(session),
      server_index_(server_index),
      socket_(std::move(socket)) {}

DnsSession::SocketLease::~SocketLease() {
  session_->FreeSocket(server_index_, std::move(socket_));
}

DnsSession::DnsSession(const DnsConfig& config,
                       std::unique_ptr<DnsSocketPool> socket_pool,
                       const RandIntCallback& rand_int_callback,
                       NetLog* net_log)
    : config_(config),
      socket_pool_(std::move(socket_pool)),
      rand_callback_(base::BindRepeating(rand_int_callback,
                                         0,
                                         std::numeric_limits<uint16_t>::max())),
      net_log_(net_log) {
  socket_pool_->Initialize(&config_.nameservers, net_log);
  UMA_HISTOGRAM_CUSTOM_COUNTS("AsyncDNS.ServerCount",
                              config_.nameservers.size(), 1, 10, 11);
}

DnsSession::~DnsSession() = default;

uint16_t DnsSession::NextQueryId() const {
  return static_cast<uint16_t>(rand_callback_.Run());
}

// Allocate a socket, already connected to the server address.
std::unique_ptr<DnsSession::SocketLease> DnsSession::AllocateSocket(
    size_t server_index,
    const NetLogSource& source) {
  std::unique_ptr<DatagramClientSocket> socket;

  socket = socket_pool_->AllocateSocket(server_index);
  if (!socket.get())
    return std::unique_ptr<SocketLease>();

  socket->NetLog().BeginEventReferencingSource(NetLogEventType::SOCKET_IN_USE,
                                               source);

  SocketLease* lease = new SocketLease(this, server_index, std::move(socket));
  return std::unique_ptr<SocketLease>(lease);
}

std::unique_ptr<StreamSocket> DnsSession::CreateTCPSocket(
    size_t server_index,
    const NetLogSource& source) {
  return socket_pool_->CreateTCPSocket(server_index, source);
}

void DnsSession::InvalidateWeakPtrsForTesting() {
  weak_ptr_factory_.InvalidateWeakPtrs();
}

// Release a socket.
void DnsSession::FreeSocket(size_t server_index,
                            std::unique_ptr<DatagramClientSocket> socket) {
  DCHECK(socket.get());

  socket->NetLog().EndEvent(NetLogEventType::SOCKET_IN_USE);

  socket_pool_->FreeSocket(server_index, std::move(socket));
}

}  // namespace net
