// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_ip_address_impl.h"

#include "net/base/address_family.h"
#include "net/quic/platform/api/quic_bug_tracker.h"

#if defined(OS_WIN)
#include <winsock2.h>
#include <ws2bth.h>
#elif defined(OS_POSIX)
#include <netinet/in.h>
#endif

using std::string;

namespace net {

QuicIpAddressImpl QuicIpAddressImpl::Loopback4() {
  return QuicIpAddressImpl(IPAddress::IPv4Localhost());
}

QuicIpAddressImpl QuicIpAddressImpl::Loopback6() {
  return QuicIpAddressImpl(IPAddress::IPv6Localhost());
}

QuicIpAddressImpl QuicIpAddressImpl::Any4() {
  return QuicIpAddressImpl(IPAddress::IPv4AllZeros());
}

QuicIpAddressImpl QuicIpAddressImpl::Any6() {
  return QuicIpAddressImpl(IPAddress::IPv6AllZeros());
}

QuicIpAddressImpl::QuicIpAddressImpl(const IPAddress& addr)
    : ip_address_(addr) {}

bool operator==(QuicIpAddressImpl lhs, QuicIpAddressImpl rhs) {
  return lhs.ip_address_ == rhs.ip_address_;
}

bool operator!=(QuicIpAddressImpl lhs, QuicIpAddressImpl rhs) {
  return !(lhs == rhs);
}

bool QuicIpAddressImpl::IsInitialized() const {
  return net::GetAddressFamily(ip_address_) != net::ADDRESS_FAMILY_UNSPECIFIED;
}

IpAddressFamily QuicIpAddressImpl::address_family() const {
  switch (net::GetAddressFamily(ip_address_)) {
    case net::ADDRESS_FAMILY_IPV4:
      return IpAddressFamily::IP_V4;
    case net::ADDRESS_FAMILY_IPV6:
      return IpAddressFamily::IP_V6;
    case net::ADDRESS_FAMILY_UNSPECIFIED:
      break;
    default:
      QUIC_BUG << "Invalid address family "
               << net::GetAddressFamily(ip_address_);
  }
  return IpAddressFamily::IP_UNSPEC;
}

int QuicIpAddressImpl::AddressFamilyToInt() const {
  switch (ip_address_.size()) {
    case IPAddress::kIPv4AddressSize:
      return AF_INET;
    case IPAddress::kIPv6AddressSize:
      return AF_INET6;
    default:
      NOTREACHED() << "Bad IP address";
      return AF_UNSPEC;
  }
}

string QuicIpAddressImpl::ToPackedString() const {
  return IPAddressToPackedString(ip_address_);
}

string QuicIpAddressImpl::ToString() const {
  if (!IsInitialized()) {
    return "Uninitialized address";
  }
  return ip_address_.ToString();
}

QuicIpAddressImpl QuicIpAddressImpl::Normalized() const {
  if (ip_address_.IsIPv4MappedIPv6()) {
    return QuicIpAddressImpl(ConvertIPv4MappedIPv6ToIPv4(ip_address_));
  }
  return QuicIpAddressImpl(ip_address_);
}

QuicIpAddressImpl QuicIpAddressImpl::DualStacked() const {
  if (ip_address_.IsIPv4()) {
    return QuicIpAddressImpl(ConvertIPv4ToIPv4MappedIPv6(ip_address_));
  }
  return QuicIpAddressImpl(ip_address_);
}

bool QuicIpAddressImpl::FromPackedString(const char* data, size_t length) {
  if (length != IPAddress::kIPv4AddressSize &&
      length != IPAddress::kIPv6AddressSize) {
    QUIC_BUG << "Invalid packed IP address of length " << length;
    return false;
  }
  ip_address_ = IPAddress(reinterpret_cast<const uint8_t*>(data), length);
  return true;
}

bool QuicIpAddressImpl::FromString(string str) {
  return ip_address_.AssignFromIPLiteral(str);
}

bool QuicIpAddressImpl::IsIPv4() const {
  return ip_address_.IsIPv4();
}

bool QuicIpAddressImpl::IsIPv6() const {
  return ip_address_.IsIPv6();
}

bool QuicIpAddressImpl::InSameSubnet(const QuicIpAddressImpl& other,
                                     int subnet_length) {
  return IPAddressMatchesPrefix(ip_address_, other.ip_address(), subnet_length);
}

}  // namespace net
