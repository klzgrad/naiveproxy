// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_IO_SOCKET_H_
#define QUICHE_QUIC_CORE_IO_SOCKET_H_

#include <functional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

#if defined(_WIN32)
#include <winsock2.h>
#endif  // defined(_WIN32)

namespace quic {

#if defined(_WIN32)
using SocketFd = SOCKET;
inline constexpr SocketFd kInvalidSocketFd = INVALID_SOCKET;
#else
using SocketFd = int;
inline constexpr SocketFd kInvalidSocketFd = -1;
#endif

// A read/write socket.
//
// Warning regarding blocking calls: Code in the QUICHE library typically
// handles IO on a single thread, so if making calls from that typical
// environment, it would be problematic to make a blocking call and block that
// single thread.
class QUICHE_EXPORT_PRIVATE Socket {
 public:
  class AsyncVisitor {
   public:
    virtual ~AsyncVisitor() = default;

    // If the operation completed without error, `data` is set to the received
    // data.
    virtual void ReceiveComplete(
        absl::StatusOr<quiche::QuicheMemSlice> data) = 0;

    virtual void SendComplete(absl::Status status) = 0;
  };

  virtual ~Socket() = default;

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

// Low-level platform-agnostic socket operations. Closely follows the behavior
// of basic POSIX socket APIs, diverging mostly only to convert to/from cleaner
// and platform-agnostic types.
namespace socket_api {
enum class SocketProtocol {
  kUdp,
  kTcp,
};

struct QUICHE_EXPORT_PRIVATE AcceptResult {
  // Socket for interacting with the accepted connection.
  SocketFd fd;

  // Address of the connected peer.
  QuicSocketAddress peer_address;
};

// Creates a socket with blocking or non-blocking behavior.
absl::StatusOr<SocketFd> CreateSocket(IpAddressFamily address_family,
                                      SocketProtocol protocol,
                                      bool blocking = false);

// Sets socket `fd` to blocking (if `blocking` true) or non-blocking (if
// `blocking` false). Must be a change from previous state.
absl::Status SetSocketBlocking(SocketFd fd, bool blocking);

// Sets buffer sizes for socket `fd` to `size` bytes.
absl::Status SetReceiveBufferSize(SocketFd fd, QuicByteCount size);
absl::Status SetSendBufferSize(SocketFd fd, QuicByteCount size);

// Connects socket `fd` to `peer_address`.  Returns a status with
// `absl::StatusCode::kUnavailable` iff the socket is non-blocking and the
// connection could not be immediately completed.  The socket will then complete
// connecting asynchronously, and on becoming writable, the result can be
// checked using GetSocketError().
absl::Status Connect(SocketFd fd, const QuicSocketAddress& peer_address);

// Gets and clears socket error information for socket `fd`. Note that returned
// error could be either the found socket error, or unusually, an error from the
// attempt to retrieve error information. Typically used to determine connection
// result after asynchronous completion of a Connect() call.
absl::Status GetSocketError(SocketFd fd);

// Assign `address` to socket `fd`.
absl::Status Bind(SocketFd fd, const QuicSocketAddress& address);

// Gets the address assigned to socket `fd`.
absl::StatusOr<QuicSocketAddress> GetSocketAddress(SocketFd fd);

// Marks socket `fd` as a passive socket listening for connection requests.
// `backlog` is the maximum number of queued connection requests. Typically
// expected to return a status with `absl::StatusCode::InvalidArgumentError`
// if `fd` is not a TCP socket.
absl::Status Listen(SocketFd fd, int backlog);

// Accepts an incoming connection to the listening socket `fd`.  The returned
// connection socket will be set as non-blocking iff `blocking` is false.
// Typically expected to return a status with
// `absl::StatusCode::InvalidArgumentError` if `fd` is not a TCP socket or not
// listening for connections.  Returns a status with
// `absl::StatusCode::kUnavailable` iff the socket is non-blocking and no
// incoming connection could be immediately accepted.
absl::StatusOr<AcceptResult> Accept(SocketFd fd, bool blocking = false);

// Receives data from socket `fd`. Will fill `buffer.data()` with up to
// `buffer.size()` bytes. On success, returns a span pointing to the buffer
// but resized to the actual number of bytes received. Returns a status with
// `absl::StatusCode::kUnavailable` iff the socket is non-blocking and the
// receive operation could not be immediately completed.  If `peek` is true,
// received data is not removed from the underlying socket data queue.
absl::StatusOr<absl::Span<char>> Receive(SocketFd fd, absl::Span<char> buffer,
                                         bool peek = false);

// Sends some or all of the data in `buffer` to socket `fd`. On success,
// returns a string_view pointing to the unsent remainder of the buffer (or an
// empty string_view if all of `buffer` was successfully sent). Returns a status
// with `absl::StatusCode::kUnavailable` iff the socket is non-blocking and the
// send operation could not be immediately completed.
absl::StatusOr<absl::string_view> Send(SocketFd fd, absl::string_view buffer);

// Closes socket `fd`.
absl::Status Close(SocketFd fd);
}  // namespace socket_api

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_IO_SOCKET_H_
