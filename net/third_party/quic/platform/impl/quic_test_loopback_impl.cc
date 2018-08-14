// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quic/platform/impl/quic_test_loopback_impl.h"

namespace quic {

IpAddressFamily AddressFamilyUnderTestImpl() {
  return IpAddressFamily::IP_V4;
}

QuicIpAddress TestLoopback4Impl() {
  return QuicIpAddress(QuicIpAddressImpl(net::IPAddress::IPv4Localhost()));
}

QuicIpAddress TestLoopback6Impl() {
  return QuicIpAddress(QuicIpAddressImpl(net::IPAddress::IPv6Localhost()));
}

QuicIpAddress TestLoopbackImpl() {
  return QuicIpAddress(QuicIpAddressImpl(net::IPAddress::IPv4Localhost()));
}

QuicIpAddress TestLoopbackImpl(int index) {
  const uint8_t kLocalhostIPv4[] = {127, 0, 0, index};
  return QuicIpAddress(QuicIpAddressImpl(net::IPAddress(kLocalhostIPv4)));
}

}  // namespace quic
