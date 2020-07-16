// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/platform/api/quic_test_loopback.h"

namespace quic {

IpAddressFamily AddressFamilyUnderTest() {
  return AddressFamilyUnderTestImpl();
}

QuicIpAddress TestLoopback4() {
  return TestLoopback4Impl();
}

QuicIpAddress TestLoopback6() {
  return TestLoopback6Impl();
}

QuicIpAddress TestLoopback() {
  return TestLoopbackImpl();
}

QuicIpAddress TestLoopback(int index) {
  return TestLoopbackImpl(index);
}

}  // namespace quic
