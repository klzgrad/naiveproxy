// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_ICMP_REACHABLE_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_ICMP_REACHABLE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/bonnet/icmp_reachable_interface.h"

namespace quic {

class MockIcmpReachable : public IcmpReachableInterface {
 public:
  MOCK_METHOD0(Init, bool());
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_ICMP_REACHABLE_H_
