// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_SOCKET_ADDRESS_H_
#define QUICHE_COMMON_QUICHE_SOCKET_ADDRESS_H_

#include <string>

#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_ip_address.h"

namespace quiche {

// A class representing a socket endpoint address (i.e., IP address plus a
// port).
class QUICHE_EXPORT QuicheSocketAddress {
 public:
  QuicheSocketAddress() {}
  QuicheSocketAddress(QuicheIpAddress address, uint16_t port);
  explicit QuicheSocketAddress(const struct sockaddr_storage& saddr);
  explicit QuicheSocketAddress(const sockaddr* saddr, socklen_t len);
  QuicheSocketAddress(const QuicheSocketAddress& other) = default;
  QuicheSocketAddress& operator=(const QuicheSocketAddress& other) = default;
  QuicheSocketAddress& operator=(QuicheSocketAddress&& other) = default;
  QUICHE_EXPORT friend bool operator==(const QuicheSocketAddress& lhs,
                                       const QuicheSocketAddress& rhs);
  QUICHE_EXPORT friend bool operator!=(const QuicheSocketAddress& lhs,
                                       const QuicheSocketAddress& rhs);

  bool IsInitialized() const;
  std::string ToString() const;

  // TODO(ericorth): Convert usage over to socket_api::GetSocketAddress() and
  // remove.
  int FromSocket(int fd);

  QuicheSocketAddress Normalized() const;

  QuicheIpAddress host() const;
  uint16_t port() const;
  sockaddr_storage generic_address() const;

  // Hashes this address to an uint32_t.
  uint32_t Hash() const;

 private:
  QuicheIpAddress host_;
  uint16_t port_ = 0;
};

inline std::ostream& operator<<(std::ostream& os,
                                const QuicheSocketAddress address) {
  os << address.ToString();
  return os;
}

// clang-format off
class QUICHE_EXPORT QuicheSocketAddressHash {
 public:
  size_t operator()(QuicheSocketAddress const& address) const noexcept {
    return address.Hash();
  }
};
// clang-format on

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_SOCKET_ADDRESS_H_
