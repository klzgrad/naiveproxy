// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <winsock2.h>

#include <cstddef>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/core/io/socket_internal.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_status_utils.h"

namespace quic::socket_api {

namespace {

using PlatformSocklen = int;
using PlatformSsizeT = int;

int SyscallGetsockopt(int sockfd, int level, int optname, void* optval,
                      int* optlen) {
  return ::getsockopt(sockfd, level, optname, reinterpret_cast<char*>(optval),
                      optlen);
}
int SyscallSetsockopt(int sockfd, int level, int optname, const void* optval,
                      int optlen) {
  return ::setsockopt(sockfd, level, optname,
                      reinterpret_cast<const char*>(optval), optlen);
}
int SyscallGetsockname(int sockfd, sockaddr* addr, int* addrlen) {
  return ::getsockname(sockfd, addr, addrlen);
}
int SyscallAccept(int sockfd, sockaddr* addr, int* addrlen) {
  return ::accept(sockfd, addr, addrlen);
}
int SyscallConnect(int sockfd, const sockaddr* addr, socklen_t addrlen) {
  return ::connect(sockfd, addr, addrlen);
}
int SyscallBind(int sockfd, const sockaddr* addr, socklen_t addrlen) {
  return ::bind(sockfd, addr, addrlen);
}
int SyscallListen(int sockfd, int backlog) { return ::listen(sockfd, backlog); }
int SyscallRecv(int sockfd, void* buf, size_t len, int flags) {
  return ::recv(sockfd, reinterpret_cast<char*>(buf), len, flags);
}
int SyscallSend(int sockfd, const void* buf, size_t len, int flags) {
  return ::send(sockfd, reinterpret_cast<const char*>(buf), len, flags);
}
int SyscallSendTo(int sockfd, const void* buf, size_t len, int flags,
                  const sockaddr* addr, socklen_t addrlen) {
  return ::sendto(sockfd, reinterpret_cast<const char*>(buf), len, flags, addr,
                  addrlen);
}

absl::StatusCode RemapErrorCode(int code) {
  switch (code) {
    // Note that kUnavailable is the special status that always has to map to
    // EWOULDBLOCK (see the API documentation).
    case WSAEWOULDBLOCK:
      return absl::StatusCode::kUnavailable;

    case WSANOTINITIALISED:
    case WSAENETDOWN:
    case WSAEAFNOSUPPORT:
    case WSAEPROTONOSUPPORT:
    case WSAESHUTDOWN:
      return absl::StatusCode::kFailedPrecondition;

    case WSAEFAULT:
    case WSAEINVAL:
    case WSAEPROTOTYPE:
    case WSAESOCKTNOSUPPORT:
    case WSAEADDRNOTAVAIL:
    case WSAENOTSOCK:
    case WSAEOPNOTSUPP:
      return absl::StatusCode::kInvalidArgument;

    case WSAEADDRINUSE:
    case WSAEISCONN:
      return absl::StatusCode::kAlreadyExists;

    case WSAEMFILE:
    case WSAENOBUFS:
      return absl::StatusCode::kResourceExhausted;

    case WSAECONNREFUSED:
    case WSAENETUNREACH:
    case WSAEHOSTUNREACH:
      return absl::StatusCode::kNotFound;

    case WSAETIMEDOUT:
      return absl::StatusCode::kDeadlineExceeded;

    case WSAEACCES:
      return absl::StatusCode::kPermissionDenied;

    case WSAENETRESET:
    case WSAECONNABORTED:
    case WSAECONNRESET:
      return absl::StatusCode::kAborted;

    case WSAEMSGSIZE:
      return absl::StatusCode::kOutOfRange;

    default:
      return absl::StatusCode::kInternal;
  }
}

std::string SystemErrorCodeToString(int code) {
  LPSTR message = nullptr;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                 nullptr, code, 0, reinterpret_cast<LPSTR>(&message), 0,
                 nullptr);
  std::string owned_message(absl::StripAsciiWhitespace(message));
  LocalFree(message);
  return owned_message;
}

absl::Status ToStatus(int error_number, absl::string_view method_name,
                      absl::flat_hash_set<int> unavailable_error_numbers = {}) {
  (void)unavailable_error_numbers;  // Suppress "unused variable" error.
  return absl::Status(
      RemapErrorCode(error_number),
      absl::StrCat(method_name, ": Winsock error ", error_number, ": ",
                   SystemErrorCodeToString(error_number)));
}

absl::Status LastSocketOperationError(
    absl::string_view method_name,
    absl::flat_hash_set<int> unavailable_error_numbers = {}) {
  return ToStatus(WSAGetLastError(), method_name, unavailable_error_numbers);
}

absl::Status StatusForLastCall(int result, absl::string_view method_name) {
  return result == 0 ? absl::OkStatus() : LastSocketOperationError(method_name);
}

}  // namespace

absl::StatusOr<SocketFd> CreateSocket(IpAddressFamily address_family,
                                      SocketProtocol protocol, bool blocking) {
  int family_int = ToPlatformAddressFamily(address_family);
  int type_int = ToPlatformSocketType(protocol);
  int protocol_int = ToPlatformProtocol(protocol);
  SocketFd result = ::WSASocket(family_int, type_int, protocol_int, nullptr, 0,
                                WSA_FLAG_OVERLAPPED);
  if (result == INVALID_SOCKET) {
    return LastSocketOperationError("socket()");
  }
  if (!blocking) {
    // Windows sockets are blocking by default.
    QUICHE_RETURN_IF_ERROR(SetSocketBlocking(result, blocking));
  }
  return result;
}

absl::Status SetSocketBlocking(SocketFd fd, bool blocking) {
  u_long mode = !blocking;
  int result = ::ioctlsocket(fd, FIONBIO, &mode);
  return StatusForLastCall(result, "SetSocketBlocking()");
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
      level = IPPROTO_IPV6;
      option = IPV6_HDRINCL;
      break;
    default:
      QUICHE_BUG(set_ip_header_included_invalid_family)
          << "Invalid address family: " << static_cast<int>(address_family);
      return absl::InvalidArgumentError("Invalid address family.");
  }

  int value = static_cast<int>(ip_header_included);
  int result = ::setsockopt(
      fd, level, option, reinterpret_cast<const char*>(&value), sizeof(value));

  if (result >= 0) {
    return absl::OkStatus();
  } else {
    absl::Status status = StatusForLastCall(result, "::setsockopt()");
    QUICHE_DVLOG(1) << "Failed to set socket " << fd << " option " << option
                    << " to " << value << " with error: " << status;
    return status;
  }
}

absl::Status Close(SocketFd fd) {
  int result = ::closesocket(fd);
  return StatusForLastCall(result, "close()");
}

}  // namespace quic::socket_api
