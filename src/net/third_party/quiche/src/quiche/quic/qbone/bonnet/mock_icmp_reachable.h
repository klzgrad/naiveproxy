// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_ICMP_REACHABLE_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_ICMP_REACHABLE_H_

#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/bonnet/icmp_reachable_interface.h"

namespace quic {

class MockIcmpReachable : public IcmpReachableInterface {
 public:
  MOCK_METHOD(bool, Init, (), (override));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_ICMP_REACHABLE_H_
