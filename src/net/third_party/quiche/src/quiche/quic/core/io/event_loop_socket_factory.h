// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_IO_EVENT_LOOP_SOCKET_FACTORY_H_
#define QUICHE_QUIC_CORE_IO_EVENT_LOOP_SOCKET_FACTORY_H_

#include <memory>

#include "quiche/quic/core/connecting_client_socket.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/socket_factory.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_buffer_allocator.h"

namespace quic {

// A socket factory that creates sockets implemented using an underlying
// QuicEventLoop.
class EventLoopSocketFactory : public SocketFactory {
 public:
  // `event_loop` and `buffer_allocator` must outlive the created factory.
  EventLoopSocketFactory(QuicEventLoop* event_loop,
                         quiche::QuicheBufferAllocator* buffer_allocator);

  // SocketFactory:
  std::unique_ptr<ConnectingClientSocket> CreateTcpClientSocket(
      const quic::QuicSocketAddress& peer_address,
      QuicByteCount receive_buffer_size, QuicByteCount send_buffer_size,
      ConnectingClientSocket::AsyncVisitor* async_visitor) override;
  std::unique_ptr<ConnectingClientSocket> CreateConnectingUdpClientSocket(
      const quic::QuicSocketAddress& peer_address,
      QuicByteCount receive_buffer_size, QuicByteCount send_buffer_size,
      ConnectingClientSocket::AsyncVisitor* async_visitor) override;

 private:
  QuicEventLoop* const event_loop_;                  // unowned
  quiche::QuicheBufferAllocator* buffer_allocator_;  // unowned
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_IO_EVENT_LOOP_SOCKET_FACTORY_H_
