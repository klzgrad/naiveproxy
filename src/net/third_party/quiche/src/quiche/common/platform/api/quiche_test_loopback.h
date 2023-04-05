// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_LOOPBACK_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_LOOPBACK_H_

#include "quiche_platform_impl/quiche_test_loopback_impl.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"

namespace quiche {

// Returns the address family (IPv4 or IPv6) used to run test under.
quic::IpAddressFamily AddressFamilyUnderTest();

// Returns an IPv4 loopback address.
quic::QuicIpAddress TestLoopback4();

// Returns the only IPv6 loopback address.
quic::QuicIpAddress TestLoopback6();

// Returns an appropriate IPv4/Ipv6 loopback address based upon whether the
// test's environment.
quic::QuicIpAddress TestLoopback();

// If address family under test is IPv4, returns an indexed IPv4 loopback
// address. If address family under test is IPv6, the address returned is
// platform-dependent.
quic::QuicIpAddress TestLoopback(int index);

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_TEST_LOOPBACK_H_
