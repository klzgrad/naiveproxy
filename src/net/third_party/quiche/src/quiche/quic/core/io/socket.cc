// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/io/socket.h"

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/socket_internal.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"

#if defined(_WIN32)
#include "quiche/quic/core/io/socket_win.inc"
#else
#include "quiche/quic/core/io/socket_posix.inc"
#endif

namespace quic::socket_api {

namespace {

absl::StatusOr<AcceptResult> AcceptInternal(SocketFd fd) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

  sockaddr_storage peer_addr;
  PlatformSocklen peer_addr_len = sizeof(peer_addr);
  SocketFd connection_socket = SyscallAccept(
      fd, reinterpret_cast<struct sockaddr*>(&peer_addr), &peer_addr_len);

  if (connection_socket == kInvalidSocketFd) {
    absl::Status status = LastSocketOperationError("::accept()");
    QUICHE_DVLOG(1) << "Failed to accept connection from socket " << fd
                    << " with error: " << status;
    return status;
  }

  absl::StatusOr<QuicSocketAddress> peer_address =
      ValidateAndConvertAddress(peer_addr, peer_addr_len);

  if (peer_address.ok()) {
    return AcceptResult{connection_socket, *peer_address};
  } else {
    return peer_address.status();
  }
}

absl::Status SetSockOptInt(SocketFd fd, int option, int value) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

  int result = SyscallSetsockopt(fd, SOL_SOCKET, option, &value, sizeof(value));

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = LastSocketOperationError("::setsockopt()");
    QUICHE_DVLOG(1) << "Failed to set socket " << fd << " option " << option
                    << " to " << value << " with error: " << status;
    return status;
  }
}

}  // namespace

absl::Status SetReceiveBufferSize(SocketFd fd, QuicByteCount size) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK_LE(size, QuicByteCount{INT_MAX});

  return SetSockOptInt(fd, SO_RCVBUF, static_cast<int>(size));
}

absl::Status SetSendBufferSize(SocketFd fd, QuicByteCount size) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK_LE(size, QuicByteCount{INT_MAX});

  return SetSockOptInt(fd, SO_SNDBUF, static_cast<int>(size));
}

absl::Status Connect(SocketFd fd, const QuicSocketAddress& peer_address) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK(peer_address.IsInitialized());

  sockaddr_storage addr = peer_address.generic_address();
  PlatformSocklen addrlen = GetAddrlen(peer_address.host().address_family());

  int connect_result =
      SyscallConnect(fd, reinterpret_cast<sockaddr*>(&addr), addrlen);

  if (connect_result >= 0) {
    return absl::OkStatus();
  } else {
    // For ::connect(), only `EINPROGRESS` indicates unavailable.
    absl::Status status =
        LastSocketOperationError("::connect()", /*unavailable_error_numbers=*/
                                 {EINPROGRESS});
    QUICHE_DVLOG(1) << "Failed to connect socket " << fd
                    << " to address: " << peer_address.ToString()
                    << " with error: " << status;
    return status;
  }
}

absl::Status GetSocketError(SocketFd fd) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

  int socket_error = 0;
  PlatformSocklen len = sizeof(socket_error);
  int sockopt_result =
      SyscallGetsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &len);

  if (sockopt_result >= 0) {
    if (socket_error == 0) {
      return absl::OkStatus();
    } else {
      return ToStatus(socket_error, "SO_ERROR");
    }
  } else {
    absl::Status status = LastSocketOperationError("::getsockopt()");
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Failed to get socket error information from socket " << fd
        << " with error: " << status;
    return status;
  }
}

absl::Status Bind(SocketFd fd, const QuicSocketAddress& address) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK(address.IsInitialized());

  sockaddr_storage addr = address.generic_address();
  PlatformSocklen addr_len = GetAddrlen(address.host().address_family());

  int result = SyscallBind(fd, reinterpret_cast<sockaddr*>(&addr), addr_len);

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = LastSocketOperationError("::bind()");
    QUICHE_DVLOG(1) << "Failed to bind socket " << fd
                    << " to address: " << address.ToString()
                    << " with error: " << status;
    return status;
  }
}

absl::StatusOr<QuicSocketAddress> GetSocketAddress(SocketFd fd) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

  sockaddr_storage addr;
  PlatformSocklen addr_len = sizeof(addr);

  int result =
      SyscallGetsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);

  if (result >= 0) {
    return ValidateAndConvertAddress(addr, addr_len);
  } else {
    absl::Status status = LastSocketOperationError("::getsockname()");
    QUICHE_DVLOG(1) << "Failed to get socket " << fd
                    << " name with error: " << status;
    return status;
  }
}

absl::Status Listen(SocketFd fd, int backlog) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK_GT(backlog, 0);

  int result = SyscallListen(fd, backlog);

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = LastSocketOperationError("::listen()");
    QUICHE_DVLOG(1) << "Failed to mark socket: " << fd
                    << " to listen with error :" << status;
    return status;
  }
}

absl::StatusOr<AcceptResult> Accept(SocketFd fd, bool blocking) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

#if defined(HAS_ACCEPT4)
  if (!blocking) {
    return AcceptWithFlags(fd, SOCK_NONBLOCK);
  }
#endif

  absl::StatusOr<AcceptResult> accept_result = AcceptInternal(fd);
  if (!accept_result.ok() || blocking) {
    return accept_result;
  }

#if !defined(__linux__) || !defined(SOCK_NONBLOCK)
  // If non-blocking could not be set directly on socket acceptance, need to
  // do it now.
  absl::Status set_non_blocking_result =
      SetSocketBlocking(accept_result->fd, /*blocking=*/false);
  if (!set_non_blocking_result.ok()) {
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Failed to set socket " << fd << " as non-blocking on acceptance.";
    if (!Close(accept_result->fd).ok()) {
      QUICHE_LOG_FIRST_N(ERROR, 100)
          << "Failed to close socket " << accept_result->fd
          << " after error setting non-blocking on acceptance.";
    }
    return set_non_blocking_result;
  }
#endif

  return accept_result;
}

absl::StatusOr<absl::Span<char>> Receive(SocketFd fd, absl::Span<char> buffer,
                                         bool peek) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK(!buffer.empty());

  PlatformSsizeT num_read = SyscallRecv(fd, buffer.data(), buffer.size(),
                                        /*flags=*/peek ? MSG_PEEK : 0);

  if (num_read > 0 && static_cast<size_t>(num_read) > buffer.size()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Received more bytes (" << num_read << ") from socket " << fd
        << " than buffer size (" << buffer.size() << ").";
    return absl::OutOfRangeError(
        "::recv(): Received more bytes than buffer size.");
  } else if (num_read >= 0) {
    return buffer.subspan(0, num_read);
  } else {
    absl::Status status = LastSocketOperationError("::recv()");
    QUICHE_DVLOG(1) << "Failed to receive from socket: " << fd
                    << " with error: " << status;
    return status;
  }
}

absl::StatusOr<absl::string_view> Send(SocketFd fd, absl::string_view buffer) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK(!buffer.empty());

  PlatformSsizeT num_sent =
      SyscallSend(fd, buffer.data(), buffer.size(), /*flags=*/0);

  if (num_sent > 0 && static_cast<size_t>(num_sent) > buffer.size()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Sent more bytes (" << num_sent << ") to socket " << fd
        << " than buffer size (" << buffer.size() << ").";
    return absl::OutOfRangeError("::send(): Sent more bytes than buffer size.");
  } else if (num_sent >= 0) {
    return buffer.substr(num_sent);
  } else {
    absl::Status status = LastSocketOperationError("::send()");
    QUICHE_DVLOG(1) << "Failed to send to socket: " << fd
                    << " with error: " << status;
    return status;
  }
}

}  // namespace quic::socket_api
