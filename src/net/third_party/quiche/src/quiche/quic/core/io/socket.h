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

// Low-level platform-agnostic socket operations. Closely follows the behavior
// of basic POSIX socket APIs, diverging mostly only to convert to/from cleaner
// and platform-agnostic types.
namespace socket_api {
enum class SocketProtocol {
  kUdp,
  kTcp,
};

inline absl::string_view GetProtocolName(SocketProtocol protocol) {
  switch (protocol) {
    case SocketProtocol::kUdp:
      return "UDP";
    case SocketProtocol::kTcp:
      return "TCP";
  }

  return "unknown";
}

struct QUICHE_EXPORT AcceptResult {
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
