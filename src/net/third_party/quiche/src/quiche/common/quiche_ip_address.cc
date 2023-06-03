// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/quiche_ip_address.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "absl/strings/str_cat.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_ip_address_family.h"

namespace quiche {

QuicheIpAddress QuicheIpAddress::Loopback4() {
  QuicheIpAddress result;
  result.family_ = IpAddressFamily::IP_V4;
  result.address_.bytes[0] = 127;
  result.address_.bytes[1] = 0;
  result.address_.bytes[2] = 0;
  result.address_.bytes[3] = 1;
  return result;
}

QuicheIpAddress QuicheIpAddress::Loopback6() {
  QuicheIpAddress result;
  result.family_ = IpAddressFamily::IP_V6;
  uint8_t* bytes = result.address_.bytes;
  memset(bytes, 0, 15);
  bytes[15] = 1;
  return result;
}

QuicheIpAddress QuicheIpAddress::Any4() {
  in_addr address;
  memset(&address, 0, sizeof(address));
  return QuicheIpAddress(address);
}

QuicheIpAddress QuicheIpAddress::Any6() {
  in6_addr address;
  memset(&address, 0, sizeof(address));
  return QuicheIpAddress(address);
}

QuicheIpAddress::QuicheIpAddress() : family_(IpAddressFamily::IP_UNSPEC) {}

QuicheIpAddress::QuicheIpAddress(const in_addr& ipv4_address)
    : family_(IpAddressFamily::IP_V4) {
  address_.v4 = ipv4_address;
}
QuicheIpAddress::QuicheIpAddress(const in6_addr& ipv6_address)
    : family_(IpAddressFamily::IP_V6) {
  address_.v6 = ipv6_address;
}

bool operator==(QuicheIpAddress lhs, QuicheIpAddress rhs) {
  if (lhs.family_ != rhs.family_) {
    return false;
  }
  switch (lhs.family_) {
    case IpAddressFamily::IP_V4:
      return std::equal(lhs.address_.bytes,
                        lhs.address_.bytes + QuicheIpAddress::kIPv4AddressSize,
                        rhs.address_.bytes);
    case IpAddressFamily::IP_V6:
      return std::equal(lhs.address_.bytes,
                        lhs.address_.bytes + QuicheIpAddress::kIPv6AddressSize,
                        rhs.address_.bytes);
    case IpAddressFamily::IP_UNSPEC:
      return true;
  }
  QUICHE_BUG(quiche_bug_10126_2)
      << "Invalid IpAddressFamily " << static_cast<int32_t>(lhs.family_);
  return false;
}

bool operator!=(QuicheIpAddress lhs, QuicheIpAddress rhs) {
  return !(lhs == rhs);
}

bool QuicheIpAddress::IsInitialized() const {
  return family_ != IpAddressFamily::IP_UNSPEC;
}

IpAddressFamily QuicheIpAddress::address_family() const { return family_; }

int QuicheIpAddress::AddressFamilyToInt() const {
  return ToPlatformAddressFamily(family_);
}

std::string QuicheIpAddress::ToPackedString() const {
  switch (family_) {
    case IpAddressFamily::IP_V4:
      return std::string(address_.chars, sizeof(address_.v4));
    case IpAddressFamily::IP_V6:
      return std::string(address_.chars, sizeof(address_.v6));
    case IpAddressFamily::IP_UNSPEC:
      return "";
  }
  QUICHE_BUG(quiche_bug_10126_3)
      << "Invalid IpAddressFamily " << static_cast<int32_t>(family_);
  return "";
}

std::string QuicheIpAddress::ToString() const {
  if (!IsInitialized()) {
    return "";
  }

  char buffer[INET6_ADDRSTRLEN] = {0};
  const char* result =
      inet_ntop(AddressFamilyToInt(), address_.bytes, buffer, sizeof(buffer));
  QUICHE_BUG_IF(quiche_bug_10126_4, result == nullptr)
      << "Failed to convert an IP address to string";
  return buffer;
}

static const uint8_t kMappedAddressPrefix[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
};

QuicheIpAddress QuicheIpAddress::Normalized() const {
  if (!IsIPv6()) {
    return *this;
  }
  if (!std::equal(std::begin(kMappedAddressPrefix),
                  std::end(kMappedAddressPrefix), address_.bytes)) {
    return *this;
  }

  in_addr result;
  memcpy(&result, &address_.bytes[12], sizeof(result));
  return QuicheIpAddress(result);
}

QuicheIpAddress QuicheIpAddress::DualStacked() const {
  if (!IsIPv4()) {
    return *this;
  }

  QuicheIpAddress result;
  result.family_ = IpAddressFamily::IP_V6;
  memcpy(result.address_.bytes, kMappedAddressPrefix,
         sizeof(kMappedAddressPrefix));
  memcpy(result.address_.bytes + 12, address_.bytes, kIPv4AddressSize);
  return result;
}

bool QuicheIpAddress::FromPackedString(const char* data, size_t length) {
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

bool QuicheIpAddress::FromString(std::string str) {
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

bool QuicheIpAddress::IsIPv4() const {
  return family_ == IpAddressFamily::IP_V4;
}

bool QuicheIpAddress::IsIPv6() const {
  return family_ == IpAddressFamily::IP_V6;
}

bool QuicheIpAddress::InSameSubnet(const QuicheIpAddress& other,
                                   int subnet_length) {
  if (!IsInitialized()) {
    QUICHE_BUG(quiche_bug_10126_5)
        << "Attempting to do subnet matching on undefined address";
    return false;
  }
  if ((IsIPv4() && subnet_length > 32) || (IsIPv6() && subnet_length > 128)) {
    QUICHE_BUG(quiche_bug_10126_6) << "Subnet mask is out of bounds";
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
  QUICHE_DCHECK_LT(static_cast<size_t>(bytes_to_check), sizeof(address_.bytes));
  int mask = (~0u) << (8u - bits_to_check);
  return (lhs[bytes_to_check] & mask) == (rhs[bytes_to_check] & mask);
}

in_addr QuicheIpAddress::GetIPv4() const {
  QUICHE_DCHECK(IsIPv4());
  return address_.v4;
}

in6_addr QuicheIpAddress::GetIPv6() const {
  QUICHE_DCHECK(IsIPv6());
  return address_.v6;
}

QuicheIpPrefix::QuicheIpPrefix() : prefix_length_(0) {}
QuicheIpPrefix::QuicheIpPrefix(const QuicheIpAddress& address)
    : address_(address) {
  if (address_.IsIPv6()) {
    prefix_length_ = QuicheIpAddress::kIPv6AddressSize * 8;
  } else if (address_.IsIPv4()) {
    prefix_length_ = QuicheIpAddress::kIPv4AddressSize * 8;
  } else {
    prefix_length_ = 0;
  }
}
QuicheIpPrefix::QuicheIpPrefix(const QuicheIpAddress& address,
                               uint8_t prefix_length)
    : address_(address), prefix_length_(prefix_length) {
  QUICHE_DCHECK(prefix_length <= QuicheIpPrefix(address).prefix_length())
      << "prefix_length cannot be longer than the size of the IP address";
}

std::string QuicheIpPrefix::ToString() const {
  return absl::StrCat(address_.ToString(), "/", prefix_length_);
}

bool operator==(const QuicheIpPrefix& lhs, const QuicheIpPrefix& rhs) {
  return lhs.address_ == rhs.address_ &&
         lhs.prefix_length_ == rhs.prefix_length_;
}

bool operator!=(const QuicheIpPrefix& lhs, const QuicheIpPrefix& rhs) {
  return !(lhs == rhs);
}

}  // namespace quiche
