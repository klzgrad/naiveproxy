// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_IO_STREAM_CLIENT_SOCKET_H_
#define QUICHE_QUIC_CORE_IO_STREAM_CLIENT_SOCKET_H_

#include "absl/status/status.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// A client socket using a protocol (typically TCP) that provides
// connection-based streams.
//
// Must not destroy a connected/connecting socket. If connected or connecting,
// must call Disconnect() to disconnect or cancel the connection before
// destruction.
//
// Warning regarding blocking calls: Code in the QUICHE library typically
// handles IO on a single thread, so if making calls from that typical
// environment, it would be problematic to make a blocking call and block that
// single thread.
class QUICHE_EXPORT_PRIVATE StreamClientSocket : public Socket {
 public:
  class AsyncVisitor : public Socket::AsyncVisitor {
   public:
    virtual void ConnectComplete(absl::Status status) = 0;
  };

  ~StreamClientSocket() override = default;

  // Establishes a connection synchronously. Should not be called if socket has
  // already been successfully connected without first calling Disconnect().
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
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_IO_STREAM_CLIENT_SOCKET_H_
