// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_ip_address.h"

#include <string>

namespace quic {

QuicIpAddress QuicIpAddress::Loopback4() {
  return QuicIpAddress(QuicIpAddressImpl::Loopback4());
}

QuicIpAddress QuicIpAddress::Loopback6() {
  return QuicIpAddress(QuicIpAddressImpl::Loopback6());
}

QuicIpAddress QuicIpAddress::Any4() {
  return QuicIpAddress(QuicIpAddressImpl::Any4());
}

QuicIpAddress QuicIpAddress::Any6() {
  return QuicIpAddress(QuicIpAddressImpl::Any6());
}

QuicIpAddress::QuicIpAddress(const QuicIpAddressImpl& impl) : impl_(impl) {}

QuicIpAddress::QuicIpAddress(const in_addr& ipv4_address)
    : impl_(ipv4_address) {}
QuicIpAddress::QuicIpAddress(const in6_addr& ipv6_address)
    : impl_(ipv6_address) {}

bool operator==(QuicIpAddress lhs, QuicIpAddress rhs) {
  return lhs.impl_ == rhs.impl_;
}

bool operator!=(QuicIpAddress lhs, QuicIpAddress rhs) {
  return !(lhs == rhs);
}

bool QuicIpAddress::IsInitialized() const {
  return impl_.IsInitialized();
}

IpAddressFamily QuicIpAddress::address_family() const {
  return impl_.address_family();
}

int QuicIpAddress::AddressFamilyToInt() const {
  return impl_.AddressFamilyToInt();
}

std::string QuicIpAddress::ToPackedString() const {
  return impl_.ToPackedString();
}

std::string QuicIpAddress::ToString() const {
  return impl_.ToString();
}

QuicIpAddress QuicIpAddress::Normalized() const {
  return QuicIpAddress(impl_.Normalized());
}

QuicIpAddress QuicIpAddress::DualStacked() const {
  return QuicIpAddress(impl_.DualStacked());
}

bool QuicIpAddress::FromPackedString(const char* data, size_t length) {
  return impl_.FromPackedString(data, length);
}

bool QuicIpAddress::FromString(std::string str) {
  return impl_.FromString(str);
}

bool QuicIpAddress::IsIPv4() const {
  return impl_.IsIPv4();
}

bool QuicIpAddress::IsIPv6() const {
  return impl_.IsIPv6();
}

bool QuicIpAddress::InSameSubnet(const QuicIpAddress& other,
                                 int subnet_length) {
  return impl_.InSameSubnet(other.impl_, subnet_length);
}

in_addr QuicIpAddress::GetIPv4() const {
  return impl_.GetIPv4();
}

in6_addr QuicIpAddress::GetIPv6() const {
  return impl_.GetIPv6();
}

}  // namespace quic
