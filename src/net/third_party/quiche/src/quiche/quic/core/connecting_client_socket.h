// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_CONNECTING_CLIENT_SOCKET_H_
#define QUICHE_QUIC_CORE_CONNECTING_CLIENT_SOCKET_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace quic {

// A client socket that provides connection-based send/receive. In the case of
// protocols like UDP, may only be a pseudo-connection that doesn't actually
// affect the underlying network protocol.
//
// Must not destroy a connected/connecting socket. If connected or connecting,
// must call Disconnect() to disconnect or cancel the connection before
// destruction.
//
// Warning regarding blocking calls: Code in the QUICHE library typically
// handles IO on a single thread, so if making calls from that typical
// environment, it would be problematic to make a blocking call and block that
// single thread.
class QUICHE_EXPORT ConnectingClientSocket {
 public:
  class AsyncVisitor {
   public:
    virtual ~AsyncVisitor() = default;

    virtual void ConnectComplete(absl::Status status) = 0;

    // If the operation completed without error, `data` is set to the received
    // data.
    virtual void ReceiveComplete(
        absl::StatusOr<quiche::QuicheMemSlice> data) = 0;

    virtual void SendComplete(absl::Status status) = 0;
  };

  virtual ~ConnectingClientSocket() = default;

  // Establishes a connection synchronously. Should not be called if socket has
  // already been successfully connected without first calling Disconnect().
  //
  // After calling, the socket must not be destroyed until Disconnect() is
  // called.
  virtual absl::Status ConnectBlocking() = 0;

  // Establishes a connection asynchronously. On completion, calls
  // ConnectComplete() on the visitor, potentially before return from
  // ConnectAsync(). Should not be called if socket has already been
  // successfully connected without first calling Disconnect().
  //
  // After calling, the socket must not be destroyed until Disconnect() is
  // called.
  virtual void ConnectAsync() = 0;

  // Disconnects a connected socket or cancels an in-progress ConnectAsync(),
  // invoking the `ConnectComplete(absl::CancelledError())` on the visitor.
  // After success, it is possible to call ConnectBlocking() or ConnectAsync()
  // again to establish a new connection. Cancels any pending read or write
  // operations, calling visitor completion methods with
  // `absl::CancelledError()`.
  //
  // Typically implemented via a call to ::close(), which for TCP can result in
  // either FIN or RST, depending on socket/platform state and undefined
  // platform behavior.
  virtual void Disconnect() = 0;

  // Gets the address assigned to a connected socket.
  virtual absl::StatusOr<QuicSocketAddress> GetLocalAddress() = 0;

  // Blocking read. Receives and returns a buffer of up to `max_size` bytes from
  // socket. Returns status on error.
  virtual absl::StatusOr<quiche::QuicheMemSlice> ReceiveBlocking(
      QuicByteCount max_size) = 0;

  // Asynchronous read. Receives up to `max_size` bytes from socket. If
  // no data is synchronously available to be read, waits until some data is
  // available or the socket is closed. On completion, calls ReceiveComplete()
  // on the visitor, potentially before return from ReceiveAsync().
  //
  // After calling, the socket must not be destroyed until ReceiveComplete() is
  // called.
  virtual void ReceiveAsync(QuicByteCount max_size) = 0;

  // Blocking write. Sends all of `data` (potentially via multiple underlying
  // socket sends).
  virtual absl::Status SendBlocking(std::string data) = 0;
  virtual absl::Status SendBlocking(quiche::QuicheMemSlice data) = 0;

  // Asynchronous write. Sends all of `data` (potentially via multiple
  // underlying socket sends). On completion, calls SendComplete() on the
  // visitor, potentially before return from SendAsync().
  //
  // After calling, the socket must not be destroyed until SendComplete() is
  // called.
  virtual void SendAsync(std::string data) = 0;
  virtual void SendAsync(quiche::QuicheMemSlice data) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_CONNECTING_CLIENT_SOCKET_H_
