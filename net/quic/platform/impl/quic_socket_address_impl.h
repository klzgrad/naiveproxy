// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_SOCKET_ADDRESS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_SOCKET_ADDRESS_IMPL_H_

#include "net/base/ip_endpoint.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/impl/quic_ip_address_impl.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicSocketAddressImpl {
 public:
  QuicSocketAddressImpl() = default;
  explicit QuicSocketAddressImpl(const IPEndPoint& addr);
  QuicSocketAddressImpl(QuicIpAddressImpl address, uint16_t port);
  explicit QuicSocketAddressImpl(const struct sockaddr_storage& saddr);
  explicit QuicSocketAddressImpl(const struct sockaddr& saddr);
  QuicSocketAddressImpl(const QuicSocketAddressImpl& other) = default;
  QuicSocketAddressImpl& operator=(const QuicSocketAddressImpl& other) =
      default;
  QuicSocketAddressImpl& operator=(QuicSocketAddressImpl&& other) = default;
  friend bool operator==(const QuicSocketAddressImpl& lhs,
                         const QuicSocketAddressImpl& rhs);
  friend bool operator!=(const QuicSocketAddressImpl& lhs,
                         const QuicSocketAddressImpl& rhs);

  bool IsInitialized() const;
  std::string ToString() const;
  int FromSocket(int fd);
  QuicSocketAddressImpl Normalized() const;

  QuicIpAddressImpl host() const;
  uint16_t port() const;

  sockaddr_storage generic_address() const;
  const IPEndPoint& socket_address() const { return socket_address_; }

 private:
  IPEndPoint socket_address_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_SOCKET_ADDRESS_IMPL_H_
