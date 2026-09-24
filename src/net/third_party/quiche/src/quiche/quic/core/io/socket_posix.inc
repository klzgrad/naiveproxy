// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <climits>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/io/socket_internal.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"

// accept4() is a Linux-specific extension that is available in glibc 2.10+.
#if defined(__linux__) && defined(_GNU_SOURCE) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 10)
#define HAS_ACCEPT4
#endif
#endif

namespace quic::socket_api {

namespace {

using PlatformSocklen = socklen_t;
using PlatformSsizeT = ssize_t;

template <typename Result, typename... Args>
Result SyscallWrapper(Result (*syscall)(Args...), Args... args) {
  while (true) {
    auto result = syscall(args...);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    return result;
  }
}

int SyscallGetsockopt(int sockfd, int level, int optname, void* optval,
                      socklen_t* optlen) {
  return SyscallWrapper(&::getsockopt, sockfd, level, optname, optval, optlen);
}
int SyscallSetsockopt(int sockfd, int level, int optname, const void* optval,
                      socklen_t optlen) {
  return SyscallWrapper(&::setsockopt, sockfd, level, optname, optval, optlen);
}
int SyscallGetsockname(int sockfd, sockaddr* addr, socklen_t* addrlen) {
  return SyscallWrapper(&::getsockname, sockfd, addr, addrlen);
}
int SyscallAccept(int sockfd, sockaddr* addr, socklen_t* addrlen) {
  return SyscallWrapper(&::accept, sockfd, addr, addrlen);
}
int SyscallConnect(int sockfd, const sockaddr* addr, socklen_t addrlen) {
  return SyscallWrapper(&::connect, sockfd, addr, addrlen);
}
int SyscallBind(int sockfd, const sockaddr* addr, socklen_t addrlen) {
  return SyscallWrapper(&::bind, sockfd, addr, addrlen);
}
int SyscallListen(int sockfd, int backlog) {
  return SyscallWrapper(&::listen, sockfd, backlog);
}
ssize_t SyscallRecv(int sockfd, void* buf, size_t len, int flags) {
  // When compiled with _FORTIFY_SOURCE, there are two overloads of recv()
  // available, which prevents SyscallWrapper from being able to infer its
  // template arguments. This currently happens in Chrome OS builds of Chrome.
  // Specify the template arguments explicitly as a workaround.
  return SyscallWrapper<ssize_t, int, void*, size_t, int>(&::recv, sockfd, buf,
                                                          len, flags);
}
ssize_t SyscallSend(int sockfd, const void* buf, size_t len, int flags) {
  // When compiled with _FORTIFY_SOURCE, there are two overloads of recv()
  // available, which prevents SyscallWrapper from being able to infer its
  // template arguments. This currently happens Android ARCore builds.
  // Specify the template arguments explicitly as a workaround.
  return SyscallWrapper<ssize_t, int, const void*, size_t, int>(
      &::send, sockfd, buf, len, flags);
}
ssize_t SyscallSendTo(int sockfd, const void* buf, size_t len, int flags,
                      const sockaddr* addr, socklen_t addrlen) {
  // When compiled with _FORTIFY_SOURCE, there are two overloads of recv()
  // available, which prevents SyscallWrapper from being able to infer its
  // template arguments. This currently happens Android ARCore builds.
  // Specify the template arguments explicitly as a workaround.
  return SyscallWrapper<ssize_t, int, const void*, size_t, int,
                        const sockaddr*, socklen_t>(
      &::sendto, sockfd, buf, len, flags, addr, addrlen);
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

absl::Status LastSocketOperationError(
    absl::string_view method_name,
    absl::flat_hash_set<int> unavailable_error_numbers = {EAGAIN,
                                                          EWOULDBLOCK}) {
  return ToStatus(errno, method_name, unavailable_error_numbers);
}

absl::Status SetSocketFlags(SocketFd fd, int to_add, int to_remove) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);
  QUICHE_DCHECK(to_add || to_remove);
  QUICHE_DCHECK(!(to_add & to_remove));

  int flags;
  do {
    flags = ::fcntl(fd, F_GETFL);
  } while (flags < 0 && errno == EINTR);
  if (flags < 0) {
    absl::Status status = LastSocketOperationError("::fcntl()");
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
    absl::Status status = LastSocketOperationError("::fcntl()");
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Could not set flags for socket " << fd << " with error: " << status;
    return status;
  }

  return absl::OkStatus();
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
    fd = SyscallWrapper(&::socket, address_family_int, type_int, protocol_int);
  } while (fd < 0 && errno == EINTR);

  if (fd >= 0) {
    return fd;
  } else {
    absl::Status status = LastSocketOperationError("::socket()");
    QUICHE_LOG_FIRST_N(ERROR, 100)
        << "Failed to create socket with error: " << status;
    return status;
  }
}

#if defined(HAS_ACCEPT4)
absl::StatusOr<AcceptResult> AcceptWithFlags(SocketFd fd, int flags) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

  sockaddr_storage peer_addr;
  socklen_t peer_addr_len = sizeof(peer_addr);
  SocketFd connection_socket;
  do {
    connection_socket = SyscallWrapper(
        &::accept4, fd, reinterpret_cast<struct sockaddr*>(&peer_addr),
        &peer_addr_len, flags);
  } while (connection_socket < 0 && errno == EINTR);

  if (connection_socket < 0) {
    absl::Status status = LastSocketOperationError("::accept4()");
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
#endif  // defined(HAS_ACCEPT4)

}  // namespace

absl::Status SetSocketBlocking(SocketFd fd, bool blocking) {
  if (blocking) {
    return SetSocketFlags(fd, /*to_add=*/0, /*to_remove=*/O_NONBLOCK);
  } else {
    return SetSocketFlags(fd, /*to_add=*/O_NONBLOCK, /*to_remove=*/0);
  }
}

absl::Status SetIpHeaderIncluded(SocketFd fd, IpAddressFamily address_family,
                                 bool ip_header_included) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

  int level;
  int option;
  switch (address_family) {
    case IpAddressFamily::IP_V4:
      level = IPPROTO_IP;
      option = IP_HDRINCL;
      break;
    case IpAddressFamily::IP_V6:
#if defined(IPPROTO_IPV6) and defined(IPV6_HDRINCL)
      level = IPPROTO_IPV6;
      option = IPV6_HDRINCL;
#else
      // If IPv6 options aren't defined, try with the IPv4 ones.
      level = IPPROTO_IP;
      option = IP_HDRINCL;
#endif
      break;
    default:
      QUICHE_BUG(set_ip_header_included_invalid_family)
          << "Invalid address family: " << static_cast<int>(address_family);
      return absl::InvalidArgumentError("Invalid address family.");
  }

  int value = static_cast<int>(ip_header_included);
  int result = ::setsockopt(fd, level, option, &value, sizeof(value));

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = LastSocketOperationError("::setsockopt()");
    QUICHE_DVLOG(1) << "Failed to set socket " << fd << " option " << option
                    << " to " << value << " with error: " << status;
    return status;
  }
}

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
      SetSocketBlocking(*socket, /*blocking=*/false);
  if (!set_non_blocking_result.ok()) {
    QUICHE_LOG_FIRST_N(ERROR, 100) << "Failed to set socket " << *socket
                                   << " as non-blocking on creation.";
    if (!Close(*socket).ok()) {
      QUICHE_LOG_FIRST_N(ERROR, 100)
          << "Failed to close socket " << *socket
          << " after set-non-blocking error on creation.";
    }
    return set_non_blocking_result;
  }
#endif

  return socket;
}

absl::Status Close(SocketFd fd) {
  QUICHE_DCHECK_NE(fd, kInvalidSocketFd);

  int close_result = ::close(fd);

  if (close_result >= 0) {
    return absl::OkStatus();
  } else if (errno == EINTR) {
    // Ignore EINTR on close because the socket is left in an undefined state
    // and can't be acted on again.
    QUICHE_DVLOG(1) << "Socket " << fd << " close unspecified due to EINTR.";
    return absl::OkStatus();
  } else {
    absl::Status status = LastSocketOperationError("::close()");
    QUICHE_DVLOG(1) << "Failed to close socket: " << fd
                    << " with error: " << status;
    return status;
  }
}

}  // namespace quic::socket_api
