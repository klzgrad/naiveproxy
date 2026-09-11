// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_CONTROLLER_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_CONTROLLER_H_

#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/bonnet/tun_device_controller.h"

namespace quic {

class MockTunDeviceController : public TunDeviceController {
 public:
  MockTunDeviceController() : TunDeviceController("", true, nullptr) {}

  MOCK_METHOD(bool, UpdateAddress, (const IpRange&), (override));

  MOCK_METHOD(bool, UpdateRoutes, (const IpRange&, const std::vector<IpRange>&),
              (override));

  MOCK_METHOD(QuicIpAddress, current_address, (), (override));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_CONTROLLER_H_
