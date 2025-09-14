// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_TEST_LOOPBACK_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_TEST_LOOPBACK_H_

#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"
#include "quiche/common/platform/api/quiche_test_loopback.h"

namespace quic {

// Returns the address family (IPv4 or IPv6) used to run test under.
inline IpAddressFamily AddressFamilyUnderTest() {
  return quiche::AddressFamilyUnderTest();
}

// Returns an IPv4 loopback address.
inline QuicIpAddress TestLoopback4() { return quiche::TestLoopback4(); }

// Returns the only IPv6 loopback address.
inline QuicIpAddress TestLoopback6() { return quiche::TestLoopback6(); }

// Returns an appropriate IPv4/Ipv6 loopback address based upon whether the
// test's environment.
inline QuicIpAddress TestLoopback() { return quiche::TestLoopback(); }

// If address family under test is IPv4, returns an indexed IPv4 loopback
// address. If address family under test is IPv6, the address returned is
// platform-dependent.
inline QuicIpAddress TestLoopback(int index) {
  return quiche::TestLoopback(index);
}

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_TEST_LOOPBACK_H_
