// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_H_

#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/bonnet/tun_device_interface.h"

namespace quic {

class MockTunDevice : public TunDeviceInterface {
 public:
  MOCK_METHOD(bool, Init, (), (override));

  MOCK_METHOD(bool, Up, (), (override));

  MOCK_METHOD(bool, Down, (), (override));

  MOCK_METHOD(void, CloseDevice, (), (override));

  MOCK_METHOD(int, GetFileDescriptor, (), (const, override));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_H_
