// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_SOCKET_ADDRESS_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_SOCKET_ADDRESS_H_

#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"

namespace quic {

// A class representing a socket endpoint address (i.e., IP address plus a
// port) in QUIC.
class QUIC_EXPORT_PRIVATE QuicSocketAddress {
 public:
  QuicSocketAddress() {}
  QuicSocketAddress(QuicIpAddress address, uint16_t port);
  explicit QuicSocketAddress(const struct sockaddr_storage& saddr);
  explicit QuicSocketAddress(const sockaddr* saddr, socklen_t len);
  QuicSocketAddress(const QuicSocketAddress& other) = default;
  QuicSocketAddress& operator=(const QuicSocketAddress& other) = default;
  QuicSocketAddress& operator=(QuicSocketAddress&& other) = default;
  QUIC_EXPORT_PRIVATE friend bool operator==(const QuicSocketAddress& lhs,
                                             const QuicSocketAddress& rhs);
  QUIC_EXPORT_PRIVATE friend bool operator!=(const QuicSocketAddress& lhs,
                                             const QuicSocketAddress& rhs);

  bool IsInitialized() const;
  std::string ToString() const;
  int FromSocket(int fd);
  QuicSocketAddress Normalized() const;

  QuicIpAddress host() const;
  uint16_t port() const;
  sockaddr_storage generic_address() const;

 private:
  QuicIpAddress host_;
  uint16_t port_ = 0;
};

inline std::ostream& operator<<(std::ostream& os,
                                const QuicSocketAddress address) {
  os << address.ToString();
  return os;
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_SOCKET_ADDRESS_H_
