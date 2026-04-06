// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TEST_LOOPBACK_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TEST_LOOPBACK_IMPL_H_

#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/platform/api/quic_ip_address_family.h"

namespace quiche {

// Returns the address family IPv4 used to run test under.
quic::IpAddressFamily AddressFamilyUnderTestImpl();

// Returns an IPv4 loopback address.
quic::QuicIpAddress TestLoopback4Impl();

// Returns the only IPv6 loopback address.
quic::QuicIpAddress TestLoopback6Impl();

// Returns an IPv4 loopback address.
quic::QuicIpAddress TestLoopbackImpl();

// Returns an indexed IPv4 loopback address.
quic::QuicIpAddress TestLoopbackImpl(int index);

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TEST_LOOPBACK_IMPL_H_
