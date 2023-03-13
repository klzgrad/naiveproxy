// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fcntl.h>
#include <sys/socket.h>

#include <climits>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"
#include "quiche/common/platform/api/quiche_logging.h"

// accept4() is a Linux-specific extension that is available in glibc 2.10+.
#if defined(__linux__) && defined(_GNU_SOURCE) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 10)
#define HAS_ACCEPT4
#endif
#endif

namespace quic::socket_api {

namespace {

int ToPlatformSocketType(SocketProtocol protocol) {
  switch (protocol) {
    case SocketProtocol::kUdp:
      return SOCK_DGRAM;
    case SocketProtocol::kTcp:
      return SOCK_STREAM;
  }

  QUICHE_NOTREACHED();
  return -1;
}

int ToPlatformProtocol(SocketProtocol protocol) {
  switch (protocol) {
    case SocketProtocol::kUdp:
      return IPPROTO_UDP;
    case SocketProtocol::kTcp:
      return IPPROTO_TCP;
  }

  QUICHE_NOTREACHED();
  return -1;
}

// Wrapper of absl::ErrnoToStatus that ensures the `unavailable_error_numbers`
// and only those numbers result in `absl::StatusCode::kUnavailable`, converting
// any other would-be-unavailable Statuses to `absl::StatusCode::kNotFound`.
absl::Status ToStatus(int error_number, absl::string_view method_name,
                      absl::flat_hash_set<int> unavailable_error_numbers = {
                          EAGAIN, EWOULDBLOCK}) {
  QUICHE_DCHECK_NE(error_number, 0);
  QUICHE_DCHECK_NE(error_number, EINTR);

  absl::Status status = absl::ErrnoToStatus(error_number, method_name);
  QUICHE_DCHECK(!status.ok());

  if (!absl::IsUnavailable(status) &&
      unavailable_error_numbers.contains(error_number)) {
    status = absl::UnavailableError(status.message());
  } else if (absl::IsUnavailable(status) &&
             !unavailable_error_numbers.contains(error_number)) {
    status = absl::NotFoundError(status.message());
  }

  return status;
}

absl::Status SetSocketFlags(SocketFd fd, int to_add, int to_remove) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK(to_add || to_remove);
  QUICHE_DCHECK(!(to_add & to_remove));

  int flags;
  do {
    flags = ::fcntl(fd, F_GETFL);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) {
    absl::Status status = ToStatus(errno, "::fcntl()");
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Could not get flags for socket " << fd << " with error: " << status;
    return status;
  }

  QUICHE_DCHECK(!(flags & to_add) || (flags & to_remove));

  int fcntl_result;
  do {
    fcntl_result = ::fcntl(fd, F_SETFL, (flags | to_add) & ~to_remove);
  } while (fcntl_result < 0 && errno == EINTR);
  if (fcntl_result < 0) {
    absl::Status status = ToStatus(errno, "::fcntl()");
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Could not set flags for socket " << fd << " with error: " << status;
    return status;
  }

  return absl::OkStatus();
}

absl::StatusOr<QuicSocketAddress> ValidateAndConvertAddress(
    const sockaddr_storage& addr, socklen_t addr_len) {
  if (addr.ss_family != AF_INET && addr.ss_family != AF_INET6) {
    QUICHE_DVLOG(1) << "Socket did not have recognized address family: "
                    << addr.ss_family;
    return absl::UnimplementedError("Unrecognized address family.");
  }

  if ((addr.ss_family == AF_INET && addr_len != sizeof(sockaddr_in)) ||
      (addr.ss_family == AF_INET6 && addr_len != sizeof(sockaddr_in6))) {
    QUICHE_DVLOG(1) << "Socket did not have expected address size ("
                    << (addr.ss_family == AF_INET ? sizeof(sockaddr_in)
                                                  : sizeof(sockaddr_in6))
                    << "), had: " << addr_len;
    return absl::UnimplementedError("Unhandled address size.");
  }

  return QuicSocketAddress(addr);
}

absl::StatusOr<SocketFd> CreateSocketWithFlags(IpAddressFamily address_family,
                                               SocketProtocol protocol,
                                               int flags) {
  int address_family_int = quiche::ToPlatformAddressFamily(address_family);

  int type_int = ToPlatformSocketType(protocol);
  type_int |= flags;

  int protocol_int = ToPlatformProtocol(protocol);

  SocketFd fd;
  do {
    fd = ::socket(address_family_int, type_int, protocol_int);
  } while (fd < 0 && errno == EINTR);

  if (fd >= 0) {
    return fd;
  } else {
    absl::Status status = ToStatus(errno, "::socket()");
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Failed to create socket with error: " << status;
    return status;
  }
}

absl::StatusOr<AcceptResult> AcceptInternal(SocketFd fd) {
  QUICHE_DCHECK_GE(fd, 0);

  sockaddr_storage peer_addr;
  socklen_t peer_addr_len = sizeof(peer_addr);
  SocketFd connection_socket;
  do {
    connection_socket = ::accept(
        fd, reinterpret_cast<struct sockaddr*>(&peer_addr), &peer_addr_len);
  } while (connection_socket < 0 && errno == EINTR);

  if (connection_socket < 0) {
    absl::Status status = ToStatus(errno, "::accept()");
    QUICHE_DVLOG(1) << "Failed to accept connection from socket " << fd
                    << " with error: " << status;
    return status;
  }

  absl::StatusOr<QuicSocketAddress> peer_address =
      ValidateAndConvertAddress(peer_addr, peer_addr_len);

  if (peer_address.ok()) {
    return AcceptResult{connection_socket, peer_address.value()};
  } else {
    return peer_address.status();
  }
}

#if defined(HAS_ACCEPT4)
absl::StatusOr<AcceptResult> AcceptWithFlags(SocketFd fd, int flags) {
  QUICHE_DCHECK_GE(fd, 0);

  sockaddr_storage peer_addr;
  socklen_t peer_addr_len = sizeof(peer_addr);
  SocketFd connection_socket;
  do {
    connection_socket =
        ::accept4(fd, reinterpret_cast<struct sockaddr*>(&peer_addr),
                  &peer_addr_len, flags);
  } while (connection_socket < 0 && errno == EINTR);

  if (connection_socket < 0) {
    absl::Status status = ToStatus(errno, "::accept4()");
    QUICHE_DVLOG(1) << "Failed to accept connection from socket " << fd
                    << " with error: " << status;
    return status;
  }

  absl::StatusOr<QuicSocketAddress> peer_address =
      ValidateAndConvertAddress(peer_addr, peer_addr_len);

  if (peer_address.ok()) {
    return AcceptResult{connection_socket, peer_address.value()};
  } else {
    return peer_address.status();
  }
}
#endif  // defined(HAS_ACCEPT4)

socklen_t GetAddrlen(IpAddressFamily family) {
  switch (family) {
    case IpAddressFamily::IP_V4:
      return sizeof(sockaddr_in);
    case IpAddressFamily::IP_V6:
      return sizeof(sockaddr_in6);
    default:
      QUICHE_NOTREACHED();
      return 0;
  }
}

absl::Status SetSockOptInt(SocketFd fd, int option, int value) {
  QUICHE_DCHECK_GE(fd, 0);

  int result;
  do {
    result = ::setsockopt(fd, SOL_SOCKET, option, &value, sizeof(value));
  } while (result < 0 && errno == EINTR);

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = ToStatus(errno, "::setsockopt()");
    QUICHE_DVLOG(1) << "Failed to set socket " << fd << " option " << option
                    << " to " << value << " with error: " << status;
    return status;
  }
}

}  // namespace

absl::StatusOr<SocketFd> CreateSocket(IpAddressFamily address_family,
                                      SocketProtocol protocol, bool blocking) {
  int flags = 0;
#if defined(__linux__) && defined(SOCK_NONBLOCK)
  if (!blocking) {
    flags = SOCK_NONBLOCK;
  }
#endif

  absl::StatusOr<SocketFd> socket =
      CreateSocketWithFlags(address_family, protocol, flags);
  if (!socket.ok() || blocking) {
    return socket;
  }

#if !defined(__linux__) || !defined(SOCK_NONBLOCK)
  // If non-blocking could not be set directly on socket creation, need to do
  // it now.
  absl::Status set_non_blocking_result =
      SetSocketBlocking(socket.value(), /*blocking=*/false);
  if (!set_non_blocking_result.ok()) {
    QUICHE_LOG_FIRST_N(ERROR, 100) << "Failed to set socket " << socket.value()
                                   << " as non-blocking on creation.";
    if (!Close(socket.value()).ok()) {
      QUICHE_LOG_FIRST_N(ERROR, 100)
          << "Failed to close socket " << socket.value()
          << " after set-non-blocking error on creation.";
    }
    return set_non_blocking_result;
  }
#endif

  return socket;
}

absl::Status SetSocketBlocking(SocketFd fd, bool blocking) {
  if (blocking) {
    return SetSocketFlags(fd, /*to_add=*/0, /*to_remove=*/O_NONBLOCK);
  } else {
    return SetSocketFlags(fd, /*to_add=*/O_NONBLOCK, /*to_remove=*/0);
  }
}

absl::Status SetReceiveBufferSize(SocketFd fd, QuicByteCount size) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK_LE(size, QuicByteCount{INT_MAX});

  return SetSockOptInt(fd, SO_RCVBUF, static_cast<int>(size));
}

absl::Status SetSendBufferSize(SocketFd fd, QuicByteCount size) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK_LE(size, QuicByteCount{INT_MAX});

  return SetSockOptInt(fd, SO_SNDBUF, static_cast<int>(size));
}

absl::Status Connect(SocketFd fd, const QuicSocketAddress& peer_address) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK(peer_address.IsInitialized());

  sockaddr_storage addr = peer_address.generic_address();
  socklen_t addrlen = GetAddrlen(peer_address.host().address_family());

  int connect_result;
  do {
    connect_result = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), addrlen);
  } while (connect_result < 0 && errno == EINTR);

  if (connect_result >= 0) {
    return absl::OkStatus();
  } else {
    // For ::connect(), only `EINPROGRESS` indicates unavailable.
    absl::Status status =
        ToStatus(errno, "::connect()", /*unavailable_error_numbers=*/
                 {EINPROGRESS});
    QUICHE_DVLOG(1) << "Failed to connect socket " << fd
                    << " to address: " << peer_address.ToString()
                    << " with error: " << status;
    return status;
  }
}

absl::Status GetSocketError(SocketFd fd) {
  QUICHE_DCHECK_GE(fd, 0);

  int socket_error = 0;
  socklen_t len = sizeof(socket_error);
  int sockopt_result;
  do {
    sockopt_result =
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &len);
  } while (sockopt_result < 0 && errno == EINTR);

  if (sockopt_result >= 0) {
    if (socket_error == 0) {
      return absl::OkStatus();
    } else {
      return ToStatus(socket_error, "SO_ERROR");
    }
  } else {
    absl::Status status = ToStatus(errno, "::getsockopt()");
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Failed to get socket error information from socket " << fd
        << " with error: " << status;
    return status;
  }
}

absl::Status Bind(SocketFd fd, const QuicSocketAddress& address) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK(address.IsInitialized());

  sockaddr_storage addr = address.generic_address();
  socklen_t addr_len = GetAddrlen(address.host().address_family());

  int result;
  do {
    result = ::bind(fd, reinterpret_cast<sockaddr*>(&addr), addr_len);
  } while (result < 0 && errno == EINTR);

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = ToStatus(errno, "::bind()");
    QUICHE_DVLOG(1) << "Failed to bind socket " << fd
                    << " to address: " << address.ToString()
                    << " with error: " << status;
    return status;
  }
}

absl::StatusOr<QuicSocketAddress> GetSocketAddress(SocketFd fd) {
  QUICHE_DCHECK_GE(fd, 0);

  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);

  int result;
  do {
    result = ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  } while (result < 0 && errno == EINTR);

  if (result >= 0) {
    return ValidateAndConvertAddress(addr, addr_len);
  } else {
    absl::Status status = ToStatus(errno, "::getsockname()");
    QUICHE_DVLOG(1) << "Failed to get socket " << fd
                    << " name with error: " << status;
    return status;
  }
}

absl::Status Listen(SocketFd fd, int backlog) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK_GT(backlog, 0);

  int result;
  do {
    result = ::listen(fd, backlog);
  } while (result < 0 && errno == EINTR);

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = ToStatus(errno, "::listen()");
    QUICHE_DVLOG(1) << "Failed to mark socket: " << fd
                    << " to listen with error :" << status;
    return status;
  }
}

absl::StatusOr<AcceptResult> Accept(SocketFd fd, bool blocking) {
  QUICHE_DCHECK_GE(fd, 0);

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
      SetSocketBlocking(accept_result.value().fd, /*blocking=*/false);
  if (!set_non_blocking_result.ok()) {
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Failed to set socket " << fd << " as non-blocking on acceptance.";
    if (!Close(accept_result.value().fd).ok()) {
      QUICHE_LOG_FIRST_N(ERROR, 100)
          << "Failed to close socket " << accept_result.value().fd
          << " after error setting non-blocking on acceptance.";
    }
    return set_non_blocking_result;
  }
#endif

  return accept_result;
}

absl::StatusOr<absl::Span<char>> Receive(SocketFd fd, absl::Span<char> buffer,
                                         bool peek) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK(!buffer.empty());

  ssize_t num_read;
  do {
    num_read =
        ::recv(fd, buffer.data(), buffer.size(), /*flags=*/peek ? MSG_PEEK : 0);
  } while (num_read < 0 && errno == EINTR);

  if (num_read > 0 && static_cast<size_t>(num_read) > buffer.size()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Received more bytes (" << num_read << ") from socket " << fd
        << " than buffer size (" << buffer.size() << ").";
    return absl::OutOfRangeError(
        "::recv(): Received more bytes than buffer size.");
  } else if (num_read >= 0) {
    return buffer.subspan(0, num_read);
  } else {
    absl::Status status = ToStatus(errno, "::recv()");
    QUICHE_DVLOG(1) << "Failed to receive from socket: " << fd
                    << " with error: " << status;
    return status;
  }
}

absl::StatusOr<absl::string_view> Send(SocketFd fd, absl::string_view buffer) {
  QUICHE_DCHECK_GE(fd, 0);
  QUICHE_DCHECK(!buffer.empty());

  ssize_t num_sent;
  do {
    num_sent = ::send(fd, buffer.data(), buffer.size(), /*flags=*/0);
  } while (num_sent < 0 && errno == EINTR);

  if (num_sent > 0 && static_cast<size_t>(num_sent) > buffer.size()) {
    QUICHE_LOG_FIRST_N(WARNING, 100)
        << "Sent more bytes (" << num_sent << ") to socket " << fd
        << " than buffer size (" << buffer.size() << ").";
    return absl::OutOfRangeError("::send(): Sent more bytes than buffer size.");
  } else if (num_sent >= 0) {
    return buffer.substr(num_sent);
  } else {
    absl::Status status = ToStatus(errno, "::send()");
    QUICHE_DVLOG(1) << "Failed to send to socket: " << fd
                    << " with error: " << status;
    return status;
  }
}

absl::Status Close(SocketFd fd) {
  QUICHE_DCHECK_GE(fd, 0);

  int close_result = ::close(fd);

  if (close_result >= 0) {
    return absl::OkStatus();
  } else if (errno == EINTR) {
    // Ignore EINTR on close because the socket is left in an undefined state
    // and can't be acted on again.
    QUICHE_DVLOG(1) << "Socket " << fd << " close unspecified due to EINTR.";
    return absl::OkStatus();
  } else {
    absl::Status status = ToStatus(errno, "::close()");
    QUICHE_DVLOG(1) << "Failed to close socket: " << fd
                    << " with error: " << status;
    return status;
  }
}

}  // namespace quic::socket_api
