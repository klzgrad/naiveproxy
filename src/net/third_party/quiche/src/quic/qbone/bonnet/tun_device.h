// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_H_
#define QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_H_

#include <string>
#include <vector>

#include "net/third_party/quiche/src/quic/qbone/bonnet/tun_device_interface.h"
#include "net/third_party/quiche/src/quic/qbone/platform/kernel_interface.h"

namespace quic {

class TunDevice : public TunDeviceInterface {
 public:
  // This represents a tun device created in the OS kernel, which is a virtual
  // network interface that any packets sent to it can be read by a user space
  // program that owns it. The routing rule that routes packets to this
  // interface should be defined somewhere else.
  //
  // Standard read/write system calls can be used to receive/send packets
  // from/to this interface. The file descriptor is owned by this class.
  //
  // If persist is set to true, the device won't be deleted even after
  // destructing. The device will be picked up when initializing this class with
  // the same interface_name on the next time.
  //
  // Persisting the device is useful if one wants to keep the routing rules
  // since once a tun device is destroyed by the kernel, all the associated
  // routing rules go away.
  //
  // The caller should own kernel and make sure it outlives this.
  TunDevice(const std::string& interface_name,
            int mtu,
            bool persist,
            bool setup_tun,
            KernelInterface* kernel);

  ~TunDevice() override;

  // Actually creates/reopens and configures the device.
  bool Init() override;

  // Marks the interface up to start receiving packets.
  bool Up() override;

  // Marks the interface down to stop receiving packets.
  bool Down() override;

  // Gets the file descriptor that can be used to send/receive packets.
  // This returns -1 when the TUN device is in an invalid state.
  int GetFileDescriptor() const override;

 private:
  // Creates or reopens the tun device.
  bool OpenDevice();

  // Configure the interface.
  bool ConfigureInterface();

  // Checks if the required kernel features exists.
  bool CheckFeatures(int tun_device_fd);

  // Closes the opened file descriptor and makes sure the file descriptor
  // is no longer available from GetFileDescriptor;
  void CleanUpFileDescriptor();

  // Opens a socket and makes netdevice ioctl call
  bool NetdeviceIoctl(int request, void* argp);

  const std::string interface_name_;
  const int mtu_;
  const bool persist_;
  const bool setup_tun_;
  int file_descriptor_;
  KernelInterface& kernel_;
  bool is_interface_up_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_H_
