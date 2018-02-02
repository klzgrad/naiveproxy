// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_TEST_LOOPBACK_H_
#define NET_QUIC_PLATFORM_API_QUIC_TEST_LOOPBACK_H_

#include "net/quic/platform/impl/quic_test_loopback_impl.h"

namespace net {

// Returns the address family (IPv4 or IPv6) used to run test under.
IpAddressFamily AddressFamilyUnderTest();

// Returns an IPv4 loopback address.
QuicIpAddress TestLoopback4();

// Returns the only IPv6 loopback address.
QuicIpAddress TestLoopback6();

// Returns an appropriate IPv4/Ipv6 loopback address based upon whether the
// test's environment.
QuicIpAddress TestLoopback();

// If address family under test is IPv4, returns an indexed IPv4 loopback
// address. If address family under test is IPv6, the address returned is
// platform-dependent.
QuicIpAddress TestLoopback(int index);

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_TEST_LOOPBACK_H_
