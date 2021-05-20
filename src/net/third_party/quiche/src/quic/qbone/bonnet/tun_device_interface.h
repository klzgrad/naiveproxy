// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_INTERFACE_H_
#define QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_INTERFACE_H_

#include <vector>

namespace quic {

// An interface with methods for interacting with a TUN device.
class TunDeviceInterface {
 public:
  virtual ~TunDeviceInterface() {}

  // Actually creates/reopens and configures the device.
  virtual bool Init() = 0;

  // Marks the interface up to start receiving packets.
  virtual bool Up() = 0;

  // Marks the interface down to stop receiving packets.
  virtual bool Down() = 0;

  // Gets the file descriptor that can be used to send/receive packets.
  // This returns -1 when the TUN device is in an invalid state.
  virtual int GetFileDescriptor() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_INTERFACE_H_
