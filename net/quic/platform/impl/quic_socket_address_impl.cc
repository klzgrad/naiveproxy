// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_socket_address_impl.h"

#include "net/base/sockaddr_storage.h"
#include "net/quic/platform/api/quic_bug_tracker.h"

using std::string;

namespace net {

QuicSocketAddressImpl::QuicSocketAddressImpl(const IPEndPoint& address)
    : socket_address_(address) {}

QuicSocketAddressImpl::QuicSocketAddressImpl(QuicIpAddressImpl address,
                                             uint16_t port)
    : socket_address_(address.ip_address(), port) {}

QuicSocketAddressImpl::QuicSocketAddressImpl(
    const struct sockaddr_storage& saddr) {
  if (saddr.ss_family == AF_INET) {
    CHECK(socket_address_.FromSockAddr(
        reinterpret_cast<const sockaddr*>(&saddr), sizeof(struct sockaddr_in)));
  } else if (saddr.ss_family == AF_INET6) {
    CHECK(
        socket_address_.FromSockAddr(reinterpret_cast<const sockaddr*>(&saddr),
                                     sizeof(struct sockaddr_in6)));
  }
}

QuicSocketAddressImpl::QuicSocketAddressImpl(const struct sockaddr& saddr) {
  QUIC_BUG << "QuicSocketAddressImpl(const struct sockaddr& saddr) is not "
              "implemented.";
}

bool operator==(const QuicSocketAddressImpl& lhs,
                const QuicSocketAddressImpl& rhs) {
  return lhs.socket_address_ == rhs.socket_address_;
}

bool operator!=(const QuicSocketAddressImpl& lhs,
                const QuicSocketAddressImpl& rhs) {
  return lhs.socket_address_.address() != rhs.socket_address_.address() ||
         lhs.socket_address_.port() != rhs.socket_address_.port();
}

bool QuicSocketAddressImpl::IsInitialized() const {
  return net::GetAddressFamily(socket_address_.address()) !=
         net::ADDRESS_FAMILY_UNSPECIFIED;
  ;
}

string QuicSocketAddressImpl::ToString() const {
  if (!IsInitialized()) {
    return "Uninitialized address";
  }
  return socket_address_.ToString();
}

int QuicSocketAddressImpl::FromSocket(int fd) {
  SockaddrStorage storage;
  if (getsockname(fd, storage.addr, &storage.addr_len) != 0 ||
      !socket_address_.FromSockAddr(storage.addr, storage.addr_len)) {
    return 1;
  }

  return 0;
}

QuicSocketAddressImpl QuicSocketAddressImpl::Normalized() const {
  QUIC_BUG << "QuicSocketAddressImpl::Normalized() is not implemented.";
  return QuicSocketAddressImpl();
}

QuicIpAddressImpl QuicSocketAddressImpl::host() const {
  return QuicIpAddressImpl(socket_address_.address());
}

uint16_t QuicSocketAddressImpl::port() const {
  return socket_address_.port();
}

sockaddr_storage QuicSocketAddressImpl::generic_address() const {
  sockaddr_storage raw_address;
  socklen_t address_len = sizeof(raw_address);
  CHECK(socket_address_.ToSockAddr(
      reinterpret_cast<struct sockaddr*>(&raw_address), &address_len));
  return raw_address;
}

}  // namespace net
