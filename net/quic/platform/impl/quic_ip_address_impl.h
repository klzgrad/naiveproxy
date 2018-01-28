// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_IP_ADDRESS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_IP_ADDRESS_IMPL_H_

#include <string>

#include "net/base/ip_address.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_ip_address_family.h"

namespace net {

class QUIC_EXPORT_PRIVATE QuicIpAddressImpl {
 public:
  enum : size_t {
    kIPv4AddressSize = IPAddress::kIPv4AddressSize,
    kIPv6AddressSize = IPAddress::kIPv6AddressSize
  };
  static QuicIpAddressImpl Loopback4();
  static QuicIpAddressImpl Loopback6();
  static QuicIpAddressImpl Any4();
  static QuicIpAddressImpl Any6();

  QuicIpAddressImpl() = default;
  QuicIpAddressImpl(const QuicIpAddressImpl& other) = default;
  explicit QuicIpAddressImpl(const IPAddress& addr);
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
  const IPAddress& ip_address() const { return ip_address_; }

 private:
  IPAddress ip_address_;
};

}  // namespace net

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_IP_ADDRESS_IMPL_H_
