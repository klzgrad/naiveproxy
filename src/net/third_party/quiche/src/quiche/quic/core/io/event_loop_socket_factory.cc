// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/io/event_loop_socket_factory.h"

#include <memory>

#include "quiche/quic/core/connecting_client_socket.h"
#include "quiche/quic/core/io/event_loop_connecting_client_socket.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace quic {

EventLoopSocketFactory::EventLoopSocketFactory(
    QuicEventLoop* event_loop, quiche::QuicheBufferAllocator* buffer_allocator)
    : event_loop_(event_loop), buffer_allocator_(buffer_allocator) {
  QUICHE_DCHECK(event_loop_);
  QUICHE_DCHECK(buffer_allocator_);
}

std::unique_ptr<ConnectingClientSocket>
EventLoopSocketFactory::CreateTcpClientSocket(
    const quic::QuicSocketAddress& peer_address,
    QuicByteCount receive_buffer_size, QuicByteCount send_buffer_size,
    ConnectingClientSocket::AsyncVisitor* async_visitor) {
  return std::make_unique<EventLoopConnectingClientSocket>(
      socket_api::SocketProtocol::kTcp, peer_address, receive_buffer_size,
      send_buffer_size, event_loop_, buffer_allocator_, async_visitor);
}

std::unique_ptr<ConnectingClientSocket>
EventLoopSocketFactory::CreateConnectingUdpClientSocket(
    const quic::QuicSocketAddress& peer_address,
    QuicByteCount receive_buffer_size, QuicByteCount send_buffer_size,
    ConnectingClientSocket::AsyncVisitor* async_visitor) {
  return std::make_unique<EventLoopConnectingClientSocket>(
      socket_api::SocketProtocol::kUdp, peer_address, receive_buffer_size,
      send_buffer_size, event_loop_, buffer_allocator_, async_visitor);
}

}  // namespace quic
