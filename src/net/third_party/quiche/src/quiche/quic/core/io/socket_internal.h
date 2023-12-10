// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Internal socket tools shared between Windows and POSIX implementations.

#ifndef QUICHE_QUIC_CORE_IO_SOCKET_INTERNAL_H_
#define QUICHE_QUIC_CORE_IO_SOCKET_INTERNAL_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "quiche/quic/core/io/socket.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic::socket_api {

inline int ToPlatformSocketType(SocketProtocol protocol) {
  switch (protocol) {
    case SocketProtocol::kUdp:
      return SOCK_DGRAM;
    case SocketProtocol::kTcp:
      return SOCK_STREAM;
  }

  QUICHE_NOTREACHED();
  return -1;
}

inline int ToPlatformProtocol(SocketProtocol protocol) {
  switch (protocol) {
    case SocketProtocol::kUdp:
      return IPPROTO_UDP;
    case SocketProtocol::kTcp:
      return IPPROTO_TCP;
  }

  QUICHE_NOTREACHED();
  return -1;
}

// A wrapper around QuicSocketAddress(sockaddr_storage) constructor that
// validates the supplied address.
inline absl::StatusOr<QuicSocketAddress> ValidateAndConvertAddress(
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

inline socklen_t GetAddrlen(IpAddressFamily family) {
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

}  // namespace quic::socket_api

#endif  // QUICHE_QUIC_CORE_IO_SOCKET_INTERNAL_H_
