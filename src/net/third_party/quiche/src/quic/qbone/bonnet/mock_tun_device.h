// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/bonnet/tun_device_interface.h"

namespace quic {

class MockTunDevice : public TunDeviceInterface {
 public:
  MOCK_METHOD0(Init, bool());

  MOCK_METHOD0(Up, bool());

  MOCK_METHOD0(Down, bool());

  MOCK_CONST_METHOD0(GetFileDescriptor, int());
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_TUN_DEVICE_H_
