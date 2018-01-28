// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/impl/quic_test_loopback_impl.h"

namespace net {

IpAddressFamily AddressFamilyUnderTestImpl() {
  return IpAddressFamily::IP_V4;
}

QuicIpAddress TestLoopback4Impl() {
  return QuicIpAddress(QuicIpAddressImpl(IPAddress::IPv4Localhost()));
}

QuicIpAddress TestLoopback6Impl() {
  return QuicIpAddress(QuicIpAddressImpl(IPAddress::IPv6Localhost()));
}

QuicIpAddress TestLoopbackImpl() {
  return QuicIpAddress(QuicIpAddressImpl(IPAddress::IPv4Localhost()));
}

QuicIpAddress TestLoopbackImpl(int index) {
  const uint8_t kLocalhostIPv4[] = {127, 0, 0, index};
  return QuicIpAddress(QuicIpAddressImpl(IPAddress(kLocalhostIPv4)));
}

}  // namespace net
