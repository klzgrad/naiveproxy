// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_IP_ADDRESS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_IP_ADDRESS_IMPL_H_

#include "build/build_config.h"

#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include "net/base/ip_address.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address_family.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QuicIpAddressImpl {
 public:
  enum : size_t {
    kIPv4AddressSize = net::IPAddress::kIPv4AddressSize,
    kIPv6AddressSize = net::IPAddress::kIPv6AddressSize
  };
  static QuicIpAddressImpl Loopback4();
  static QuicIpAddressImpl Loopback6();
  static QuicIpAddressImpl Any4();
  static QuicIpAddressImpl Any6();

  QuicIpAddressImpl() = default;
  QuicIpAddressImpl(const QuicIpAddressImpl& other) = default;
  explicit QuicIpAddressImpl(const net::IPAddress& addr);
  explicit QuicIpAddressImpl(const in_addr& ipv4_address);
  explicit QuicIpAddressImpl(const in6_addr& ipv6_address);
  QuicIpAddressImpl& operator=(const QuicIpAddressImpl& other) = default;
  QuicIpAddressImpl& operator=(QuicIpAddressImpl&& other) = default;
  friend bool operator==(QuicIpAddressImpl lhs, QuicIpAddressImpl rhs);
  friend bool operator!=(QuicIpAddressImpl lhs, QuicIpAddressImpl rhs);

  bool IsInitialized() const;
  IpAddressFamily address_family() const;
  int AddressFamilyToInt() const;
  std::string ToPackedString() const;
  std::string ToString() const;
  QuicIpAddressImpl Normalized() const;
  QuicIpAddressImpl DualStacked() const;
  bool FromPackedString(const char* data, size_t length);
  bool FromString(std::string str);
  bool IsIPv4() const;
  bool IsIPv6() const;
  bool InSameSubnet(const QuicIpAddressImpl& other, int subnet_length);

  in_addr GetIPv4() const;
  in6_addr GetIPv6() const;
  const net::IPAddress& ip_address() const { return ip_address_; }

 private:
  net::IPAddress ip_address_;
};

}  // namespace quic

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_IP_ADDRESS_IMPL_H_
