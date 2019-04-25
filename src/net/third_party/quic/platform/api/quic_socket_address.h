// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SOCKET_ADDRESS_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SOCKET_ADDRESS_H_

#include "net/third_party/quic/platform/api/quic_export.h"
#include "net/third_party/quic/platform/api/quic_ip_address.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/impl/quic_socket_address_impl.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicSocketAddress {
  // A class representing a socket endpoint address (i.e., IP address plus a
  // port) in QUIC. The actual implementation (platform dependent) of a socket
  // address is in QuicSocketAddressImpl.
 public:
  QuicSocketAddress() = default;
  QuicSocketAddress(QuicIpAddress address, uint16_t port);
  explicit QuicSocketAddress(const struct sockaddr_storage& saddr);
  explicit QuicSocketAddress(const struct sockaddr& saddr);
  explicit QuicSocketAddress(const QuicSocketAddressImpl& impl);
  QuicSocketAddress(const QuicSocketAddress& other) = default;
  QuicSocketAddress& operator=(const QuicSocketAddress& other) = default;
  QuicSocketAddress& operator=(QuicSocketAddress&& other) = default;
  QUIC_EXPORT_PRIVATE friend bool operator==(const QuicSocketAddress& lhs,
                                             const QuicSocketAddress& rhs);
  QUIC_EXPORT_PRIVATE friend bool operator!=(const QuicSocketAddress& lhs,
                                             const QuicSocketAddress& rhs);

  bool IsInitialized() const;
  QuicString ToString() const;
  int FromSocket(int fd);
  QuicSocketAddress Normalized() const;

  QuicIpAddress host() const;
  uint16_t port() const;
  sockaddr_storage generic_address() const;
  const QuicSocketAddressImpl& impl() const { return impl_; }

 private:
  QuicSocketAddressImpl impl_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_SOCKET_ADDRESS_H_
