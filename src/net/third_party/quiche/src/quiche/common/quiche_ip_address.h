// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_QUICHE_IP_ADDRESS_H_
#define QUICHE_COMMON_QUICHE_IP_ADDRESS_H_

#include <cstdint>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#include <ostream>
#include <string>

#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_ip_address_family.h"

namespace quiche {

// Represents an IP address.
class QUICHE_EXPORT QuicheIpAddress {
 public:
  // Sizes of IP addresses of different types, in bytes.
  enum : size_t {
    kIPv4AddressSize = 32 / 8,
    kIPv6AddressSize = 128 / 8,
    kMaxAddressSize = kIPv6AddressSize,
  };

  // TODO(fayang): Remove Loopback*() and use TestLoopback*() in tests.
  static QuicheIpAddress Loopback4();
  static QuicheIpAddress Loopback6();
  static QuicheIpAddress Any4();
  static QuicheIpAddress Any6();

  QuicheIpAddress();
  QuicheIpAddress(const QuicheIpAddress& other) = default;
  explicit QuicheIpAddress(const in_addr& ipv4_address);
  explicit QuicheIpAddress(const in6_addr& ipv6_address);
  QuicheIpAddress& operator=(const QuicheIpAddress& other) = default;
  QuicheIpAddress& operator=(QuicheIpAddress&& other) = default;
  QUICHE_EXPORT friend bool operator==(QuicheIpAddress lhs,
                                       QuicheIpAddress rhs);
  QUICHE_EXPORT friend bool operator!=(QuicheIpAddress lhs,
                                       QuicheIpAddress rhs);

  bool IsInitialized() const;
  IpAddressFamily address_family() const;
  int AddressFamilyToInt() const;
  // Returns the address as a sequence of bytes in network-byte-order. IPv4 will
  // be 4 bytes. IPv6 will be 16 bytes.
  std::string ToPackedString() const;
  // Returns string representation of the address.
  std::string ToString() const;
  // Normalizes the address representation with respect to IPv4 addresses, i.e,
  // mapped IPv4 addresses ("::ffff:X.Y.Z.Q") are converted to pure IPv4
  // addresses.  All other IPv4, IPv6, and empty values are left unchanged.
  QuicheIpAddress Normalized() const;
  // Returns an address suitable for use in IPv6-aware contexts.  This is the
  // opposite of NormalizeIPAddress() above.  IPv4 addresses are converted into
  // their IPv4-mapped address equivalents (e.g. 192.0.2.1 becomes
  // ::ffff:192.0.2.1).  IPv6 addresses are a noop (they are returned
  // unchanged).
  QuicheIpAddress DualStacked() const;
  bool FromPackedString(const char* data, size_t length);
  bool FromString(std::string str);
  bool IsIPv4() const;
  bool IsIPv6() const;
  bool InSameSubnet(const QuicheIpAddress& other, int subnet_length);

  in_addr GetIPv4() const;
  in6_addr GetIPv6() const;

 private:
  union {
    in_addr v4;
    in6_addr v6;
    uint8_t bytes[kMaxAddressSize];
    char chars[kMaxAddressSize];
  } address_;
  IpAddressFamily family_;
};

inline std::ostream& operator<<(std::ostream& os,
                                const QuicheIpAddress address) {
  os << address.ToString();
  return os;
}

// Represents an IP prefix, which is an IP address and a prefix length in bits.
class QUICHE_EXPORT QuicheIpPrefix {
 public:
  QuicheIpPrefix();
  explicit QuicheIpPrefix(const QuicheIpAddress& address);
  explicit QuicheIpPrefix(const QuicheIpAddress& address,
                          uint8_t prefix_length);

  QuicheIpAddress address() const { return address_; }
  uint8_t prefix_length() const { return prefix_length_; }
  // Human-readable string representation of the prefix suitable for logging.
  std::string ToString() const;

  QuicheIpPrefix(const QuicheIpPrefix& other) = default;
  QuicheIpPrefix& operator=(const QuicheIpPrefix& other) = default;
  QuicheIpPrefix& operator=(QuicheIpPrefix&& other) = default;
  QUICHE_EXPORT friend bool operator==(const QuicheIpPrefix& lhs,
                                       const QuicheIpPrefix& rhs);
  QUICHE_EXPORT friend bool operator!=(const QuicheIpPrefix& lhs,
                                       const QuicheIpPrefix& rhs);

 private:
  QuicheIpAddress address_;
  uint8_t prefix_length_;
};

inline std::ostream& operator<<(std::ostream& os, const QuicheIpPrefix prefix) {
  os << prefix.ToString();
  return os;
}

}  // namespace quiche

#endif  // QUICHE_COMMON_QUICHE_IP_ADDRESS_H_
