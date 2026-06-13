// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/platform/api/quiche_test_loopback.h"

namespace quiche {

quic::IpAddressFamily AddressFamilyUnderTest() {
  return AddressFamilyUnderTestImpl();
}

quic::QuicIpAddress TestLoopback4() { return TestLoopback4Impl(); }

quic::QuicIpAddress TestLoopback6() { return TestLoopback6Impl(); }

quic::QuicIpAddress TestLoopback() { return TestLoopbackImpl(); }

quic::QuicIpAddress TestLoopback(int index) { return TestLoopbackImpl(index); }

}  // namespace quiche
