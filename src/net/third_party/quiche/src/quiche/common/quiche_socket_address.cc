// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_socket_address.h"

#include <cstring>
#include <limits>
#include <string>

#include "absl/strings/str_cat.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/quiche_ip_address.h"
#include "quiche/common/quiche_ip_address_family.h"

namespace quiche {

namespace {

uint32_t HashIP(const QuicheIpAddress& ip) {
  if (ip.IsIPv4()) {
    return ip.GetIPv4().s_addr;
  }
  if (ip.IsIPv6()) {
    auto v6addr = ip.GetIPv6();
    const uint32_t* v6_as_ints =
        reinterpret_cast<const uint32_t*>(&v6addr.s6_addr);
    return v6_as_ints[0] ^ v6_as_ints[1] ^ v6_as_ints[2] ^ v6_as_ints[3];
  }
  return 0;
}

}  // namespace

QuicheSocketAddress::QuicheSocketAddress(QuicheIpAddress address, uint16_t port)
    : host_(address), port_(port) {}

QuicheSocketAddress::QuicheSocketAddress(const struct sockaddr_storage& saddr) {
  switch (saddr.ss_family) {
    case AF_INET: {
      const sockaddr_in* v4 = reinterpret_cast<const sockaddr_in*>(&saddr);
      host_ = QuicheIpAddress(v4->sin_addr);
      port_ = ntohs(v4->sin_port);
      break;
    }
    case AF_INET6: {
      const sockaddr_in6* v6 = reinterpret_cast<const sockaddr_in6*>(&saddr);
      host_ = QuicheIpAddress(v6->sin6_addr);
      port_ = ntohs(v6->sin6_port);
      break;
    }
    default:
      QUICHE_BUG(unknown_address_family)
          << "Unknown address family passed: " << saddr.ss_family;
      break;
  }
}

QuicheSocketAddress::QuicheSocketAddress(const sockaddr* saddr, socklen_t len) {
  sockaddr_storage storage;
  static_assert(std::numeric_limits<socklen_t>::max() >= sizeof(storage),
                "Cannot cast sizeof(storage) to socklen_t as it does not fit");
  if (len < static_cast<socklen_t>(sizeof(sockaddr)) ||
      (saddr->sa_family == AF_INET &&
       len < static_cast<socklen_t>(sizeof(sockaddr_in))) ||
      (saddr->sa_family == AF_INET6 &&
       len < static_cast<socklen_t>(sizeof(sockaddr_in6))) ||
      len > static_cast<socklen_t>(sizeof(storage))) {
    QUICHE_BUG(socket_address_bad_length)
        << "Socket address of invalid length provided";
    return;
  }
  memcpy(&storage, saddr, len);
  *this = QuicheSocketAddress(storage);
}

bool operator==(const QuicheSocketAddress& lhs,
                const QuicheSocketAddress& rhs) {
  return lhs.host_ == rhs.host_ && lhs.port_ == rhs.port_;
}

bool operator!=(const QuicheSocketAddress& lhs,
                const QuicheSocketAddress& rhs) {
  return !(lhs == rhs);
}

bool QuicheSocketAddress::IsInitialized() const {
  return host_.IsInitialized();
}

std::string QuicheSocketAddress::ToString() const {
  switch (host_.address_family()) {
    case IpAddressFamily::IP_V4:
      return absl::StrCat(host_.ToString(), ":", port_);
    case IpAddressFamily::IP_V6:
      return absl::StrCat("[", host_.ToString(), "]:", port_);
    default:
      return "";
  }
}

int QuicheSocketAddress::FromSocket(int fd) {
  sockaddr_storage addr;
  socklen_t addr_len = sizeof(addr);
  int result = getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);

  bool success = result == 0 && addr_len > 0 &&
                 static_cast<size_t>(addr_len) <= sizeof(addr);
  if (success) {
    *this = QuicheSocketAddress(addr);
    return 0;
  }
  return -1;
}

QuicheSocketAddress QuicheSocketAddress::Normalized() const {
  return QuicheSocketAddress(host_.Normalized(), port_);
}

QuicheIpAddress QuicheSocketAddress::host() const { return host_; }

uint16_t QuicheSocketAddress::port() const { return port_; }

sockaddr_storage QuicheSocketAddress::generic_address() const {
  union {
    sockaddr_storage storage;
    sockaddr_in v4;
    sockaddr_in6 v6;
  } result;
  memset(&result.storage, 0, sizeof(result.storage));

  switch (host_.address_family()) {
    case IpAddressFamily::IP_V4:
      result.v4.sin_family = AF_INET;
      result.v4.sin_addr = host_.GetIPv4();
      result.v4.sin_port = htons(port_);
      break;
    case IpAddressFamily::IP_V6:
      result.v6.sin6_family = AF_INET6;
      result.v6.sin6_addr = host_.GetIPv6();
      result.v6.sin6_port = htons(port_);
      break;
    default:
      result.storage.ss_family = AF_UNSPEC;
      break;
  }
  return result.storage;
}

uint32_t QuicheSocketAddress::Hash() const {
  uint32_t value = 0;
  value ^= HashIP(host_);
  value ^= port_ | (port_ << 16);
  return value;
}

}  // namespace quiche
