// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"

namespace quic {

static int ToPlatformAddressFamily(IpAddressFamily family) {
  switch (family) {
    case IpAddressFamily::IP_V4:
      return AF_INET;
    case IpAddressFamily::IP_V6:
      return AF_INET6;
    case IpAddressFamily::IP_UNSPEC:
      return AF_UNSPEC;
  }
  QUIC_BUG << "Invalid IpAddressFamily " << static_cast<int32_t>(family);
  return AF_UNSPEC;
}

QuicIpAddress QuicIpAddress::Loopback4() {
  QuicIpAddress result;
  result.family_ = IpAddressFamily::IP_V4;
  result.address_.bytes[0] = 127;
  result.address_.bytes[1] = 0;
  result.address_.bytes[2] = 0;
  result.address_.bytes[3] = 1;
  return result;
}

QuicIpAddress QuicIpAddress::Loopback6() {
  QuicIpAddress result;
  result.family_ = IpAddressFamily::IP_V6;
  uint8_t* bytes = result.address_.bytes;
  memset(bytes, 0, 15);
  bytes[15] = 1;
  return result;
}

QuicIpAddress QuicIpAddress::Any4() {
  in_addr address;
  memset(&address, 0, sizeof(address));
  return QuicIpAddress(address);
}

QuicIpAddress QuicIpAddress::Any6() {
  in6_addr address;
  memset(&address, 0, sizeof(address));
  return QuicIpAddress(address);
}

QuicIpAddress::QuicIpAddress() : family_(IpAddressFamily::IP_UNSPEC) {}

QuicIpAddress::QuicIpAddress(const in_addr& ipv4_address)
    : family_(IpAddressFamily::IP_V4) {
  address_.v4 = ipv4_address;
}
QuicIpAddress::QuicIpAddress(const in6_addr& ipv6_address)
    : family_(IpAddressFamily::IP_V6) {
  address_.v6 = ipv6_address;
}

bool operator==(QuicIpAddress lhs, QuicIpAddress rhs) {
  if (lhs.family_ != rhs.family_) {
    return false;
  }
  switch (lhs.family_) {
    case IpAddressFamily::IP_V4:
      return std::equal(lhs.address_.bytes,
                        lhs.address_.bytes + QuicIpAddress::kIPv4AddressSize,
                        rhs.address_.bytes);
    case IpAddressFamily::IP_V6:
      return std::equal(lhs.address_.bytes,
                        lhs.address_.bytes + QuicIpAddress::kIPv6AddressSize,
                        rhs.address_.bytes);
    case IpAddressFamily::IP_UNSPEC:
      return true;
  }
  QUIC_BUG << "Invalid IpAddressFamily " << static_cast<int32_t>(lhs.family_);
  return false;
}

bool operator!=(QuicIpAddress lhs, QuicIpAddress rhs) {
  return !(lhs == rhs);
}

bool QuicIpAddress::IsInitialized() const {
  return family_ != IpAddressFamily::IP_UNSPEC;
}

IpAddressFamily QuicIpAddress::address_family() const {
  return family_;
}

int QuicIpAddress::AddressFamilyToInt() const {
  return ToPlatformAddressFamily(family_);
}

std::string QuicIpAddress::ToPackedString() const {
  switch (family_) {
    case IpAddressFamily::IP_V4:
      return std::string(address_.chars, sizeof(address_.v4));
    case IpAddressFamily::IP_V6:
      return std::string(address_.chars, sizeof(address_.v6));
    case IpAddressFamily::IP_UNSPEC:
      return "";
  }
  QUIC_BUG << "Invalid IpAddressFamily " << static_cast<int32_t>(family_);
  return "";
}

std::string QuicIpAddress::ToString() const {
  if (!IsInitialized()) {
    return "";
  }

  char buffer[INET6_ADDRSTRLEN] = {0};
  const char* result =
      inet_ntop(AddressFamilyToInt(), address_.bytes, buffer, sizeof(buffer));
  QUIC_BUG_IF(result == nullptr) << "Failed to convert an IP address to string";
  return buffer;
}

static const uint8_t kMappedAddressPrefix[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
};

QuicIpAddress QuicIpAddress::Normalized() const {
  if (!IsIPv6()) {
    return *this;
  }
  if (!std::equal(std::begin(kMappedAddressPrefix),
                  std::end(kMappedAddressPrefix), address_.bytes)) {
    return *this;
  }

  in_addr result;
  memcpy(&result, &address_.bytes[12], sizeof(result));
  return QuicIpAddress(result);
}

QuicIpAddress QuicIpAddress::DualStacked() const {
  if (!IsIPv4()) {
    return *this;
  }

  QuicIpAddress result;
  result.family_ = IpAddressFamily::IP_V6;
  memcpy(result.address_.bytes, kMappedAddressPrefix,
         sizeof(kMappedAddressPrefix));
  memcpy(result.address_.bytes + 12, address_.bytes, kIPv4AddressSize);
  return result;
}

bool QuicIpAddress::FromPackedString(const char* data, size_t length) {
  switch (length) {
    case kIPv4AddressSize:
      family_ = IpAddressFamily::IP_V4;
      break;
    case kIPv6AddressSize:
      family_ = IpAddressFamily::IP_V6;
      break;
    default:
      return false;
  }
  memcpy(address_.chars, data, length);
  return true;
}

bool QuicIpAddress::FromString(std::string str) {
  for (IpAddressFamily family :
       {IpAddressFamily::IP_V6, IpAddressFamily::IP_V4}) {
    int result =
        inet_pton(ToPlatformAddressFamily(family), str.c_str(), address_.bytes);
    if (result > 0) {
      family_ = family;
      return true;
    }
  }
  return false;
}

bool QuicIpAddress::IsIPv4() const {
  return family_ == IpAddressFamily::IP_V4;
}

bool QuicIpAddress::IsIPv6() const {
  return family_ == IpAddressFamily::IP_V6;
}

bool QuicIpAddress::InSameSubnet(const QuicIpAddress& other,
                                 int subnet_length) {
  if (!IsInitialized()) {
    QUIC_BUG << "Attempting to do subnet matching on undefined address";
    return false;
  }
  if ((IsIPv4() && subnet_length > 32) || (IsIPv6() && subnet_length > 128)) {
    QUIC_BUG << "Subnet mask is out of bounds";
    return false;
  }

  int bytes_to_check = subnet_length / 8;
  int bits_to_check = subnet_length % 8;
  const uint8_t* const lhs = address_.bytes;
  const uint8_t* const rhs = other.address_.bytes;
  if (!std::equal(lhs, lhs + bytes_to_check, rhs)) {
    return false;
  }
  if (bits_to_check == 0) {
    return true;
  }
  DCHECK_LT(static_cast<size_t>(bytes_to_check), sizeof(address_.bytes));
  int mask = (~0u) << (8u - bits_to_check);
  return (lhs[bytes_to_check] & mask) == (rhs[bytes_to_check] & mask);
}

in_addr QuicIpAddress::GetIPv4() const {
  DCHECK(IsIPv4());
  return address_.v4;
}

in6_addr QuicIpAddress::GetIPv6() const {
  DCHECK(IsIPv6());
  return address_.v6;
}

}  // namespace quic
