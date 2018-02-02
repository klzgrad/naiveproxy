// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_socket_address.h"

using std::string;

namespace net {

QuicSocketAddress::QuicSocketAddress(QuicIpAddress address, uint16_t port)
    : impl_(address.impl(), port) {}

QuicSocketAddress::QuicSocketAddress(const struct sockaddr_storage& saddr)
    : impl_(saddr) {}

QuicSocketAddress::QuicSocketAddress(const struct sockaddr& saddr)
    : impl_(saddr) {}

QuicSocketAddress::QuicSocketAddress(const QuicSocketAddressImpl& impl)
    : impl_(impl) {}

bool operator==(const QuicSocketAddress& lhs, const QuicSocketAddress& rhs) {
  return lhs.impl_ == rhs.impl_;
}

bool operator!=(const QuicSocketAddress& lhs, const QuicSocketAddress& rhs) {
  return lhs.impl_ != rhs.impl_;
}

bool QuicSocketAddress::IsInitialized() const {
  return impl_.IsInitialized();
}

string QuicSocketAddress::ToString() const {
  return impl_.ToString();
}

int QuicSocketAddress::FromSocket(int fd) {
  return impl_.FromSocket(fd);
}

QuicSocketAddress QuicSocketAddress::Normalized() const {
  return QuicSocketAddress(impl_.Normalized());
}

QuicIpAddress QuicSocketAddress::host() const {
  return QuicIpAddress(impl_.host());
}

uint16_t QuicSocketAddress::port() const {
  return impl_.port();
}

sockaddr_storage QuicSocketAddress::generic_address() const {
  return impl_.generic_address();
}

}  // namespace net
