// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_SOCKET_FACTORY_H_
#define QUICHE_QUIC_CORE_SOCKET_FACTORY_H_

#include <memory>

#include "quiche/quic/core/connecting_client_socket.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// A factory to create objects of type Socket and derived interfaces.
class QUICHE_EXPORT SocketFactory {
 public:
  virtual ~SocketFactory() = default;

  // Will use platform default buffer size if `receive_buffer_size` or
  // `send_buffer_size` is zero. If `async_visitor` is null, async operations
  // must not be called on the created socket. If `async_visitor` is non-null,
  // it must outlive the created socket.
  virtual std::unique_ptr<ConnectingClientSocket> CreateTcpClientSocket(
      const quic::QuicSocketAddress& peer_address,
      QuicByteCount receive_buffer_size, QuicByteCount send_buffer_size,
      ConnectingClientSocket::AsyncVisitor* async_visitor) = 0;

  // Will use platform default buffer size if `receive_buffer_size` or
  // `send_buffer_size` is zero. If `async_visitor` is null, async operations
  // must not be called on the created socket. If `async_visitor` is non-null,
  // it must outlive the created socket.
  //
  // TODO(ericorth): Consider creating a sub-interface for connecting UDP
  // sockets with additional functionality, e.g. sendto, if needed.
  virtual std::unique_ptr<ConnectingClientSocket>
  CreateConnectingUdpClientSocket(
      const quic::QuicSocketAddress& peer_address,
      QuicByteCount receive_buffer_size, QuicByteCount send_buffer_size,
      ConnectingClientSocket::AsyncVisitor* async_visitor) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_SOCKET_FACTORY_H_
