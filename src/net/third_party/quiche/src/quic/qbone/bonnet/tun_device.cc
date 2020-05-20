// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/bonnet/tun_device.h"

#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/qbone/platform/kernel_interface.h"

namespace quic {

const char kTapTunDevicePath[] = "/dev/net/tun";
const int kInvalidFd = -1;

TunDevice::TunDevice(const std::string& interface_name,
                     int mtu,
                     bool persist,
                     bool setup_tun,
                     KernelInterface* kernel)
    : interface_name_(interface_name),
      mtu_(mtu),
      persist_(persist),
      setup_tun_(setup_tun),
      file_descriptor_(kInvalidFd),
      kernel_(*kernel) {}

TunDevice::~TunDevice() {
  if (!persist_) {
    Down();
  }
  CleanUpFileDescriptor();
}

bool TunDevice::Init() {
  if (interface_name_.empty() || interface_name_.size() >= IFNAMSIZ) {
    QUIC_BUG << "interface_name must be nonempty and shorter than " << IFNAMSIZ;
    return false;
  }

  if (!OpenDevice()) {
    return false;
  }

  if (!ConfigureInterface()) {
    return false;
  }

  return true;
}

// TODO(pengg): might be better to use netlink socket, once we have a library to
// use
bool TunDevice::Up() {
  if (setup_tun_ && !is_interface_up_) {
    struct ifreq if_request;
    memset(&if_request, 0, sizeof(if_request));
    // copy does not zero-terminate the result string, but we've memset the
    // entire struct.
    interface_name_.copy(if_request.ifr_name, IFNAMSIZ);
    if_request.ifr_flags = IFF_UP;

    is_interface_up_ =
        NetdeviceIoctl(SIOCSIFFLAGS, reinterpret_cast<void*>(&if_request));
    return is_interface_up_;
  } else {
    return true;
  }
}

// TODO(pengg): might be better to use netlink socket, once we have a library to
// use
bool TunDevice::Down() {
  if (setup_tun_ && is_interface_up_) {
    struct ifreq if_request;
    memset(&if_request, 0, sizeof(if_request));
    // copy does not zero-terminate the result string, but we've memset the
    // entire struct.
    interface_name_.copy(if_request.ifr_name, IFNAMSIZ);
    if_request.ifr_flags = 0;

    is_interface_up_ =
        !NetdeviceIoctl(SIOCSIFFLAGS, reinterpret_cast<void*>(&if_request));
    return !is_interface_up_;
  } else {
    return true;
  }
}

int TunDevice::GetFileDescriptor() const {
  return file_descriptor_;
}

bool TunDevice::OpenDevice() {
  struct ifreq if_request;
  memset(&if_request, 0, sizeof(if_request));
  // copy does not zero-terminate the result string, but we've memset the entire
  // struct.
  interface_name_.copy(if_request.ifr_name, IFNAMSIZ);

  // Always set IFF_MULTI_QUEUE since a persistent device does not allow this
  // flag to be flipped when re-opening it. The only way to flip this flag is to
  // destroy the device and create a new one, but that deletes any existing
  // routing associated with the interface, which makes the meaning of the
  // 'persist' bit ambiguous.
  if_request.ifr_flags = IFF_TUN | IFF_MULTI_QUEUE | IFF_NO_PI;

  // TODO(pengg): port MakeCleanup to quic/platform? This makes the call to
  // CleanUpFileDescriptor nicer and less error-prone.
  // When the device is running with IFF_MULTI_QUEUE set, each call to open will
  // create a queue which can be used to read/write packets from/to the device.
  int fd = kernel_.open(kTapTunDevicePath, O_RDWR);
  if (fd < 0) {
    QUIC_PLOG(WARNING) << "Failed to open " << kTapTunDevicePath;
    CleanUpFileDescriptor();
    return false;
  }
  file_descriptor_ = fd;
  if (!CheckFeatures(fd)) {
    CleanUpFileDescriptor();
    return false;
  }

  if (kernel_.ioctl(fd, TUNSETIFF, reinterpret_cast<void*>(&if_request)) != 0) {
    QUIC_PLOG(WARNING) << "Failed to TUNSETIFF on fd(" << fd << ")";
    CleanUpFileDescriptor();
    return false;
  }

  if (kernel_.ioctl(
          fd, TUNSETPERSIST,
          persist_ ? reinterpret_cast<void*>(&if_request) : nullptr) != 0) {
    QUIC_PLOG(WARNING) << "Failed to TUNSETPERSIST on fd(" << fd << ")";
    CleanUpFileDescriptor();
    return false;
  }

  return true;
}

// TODO(pengg): might be better to use netlink socket, once we have a library to
// use
bool TunDevice::ConfigureInterface() {
  if (!setup_tun_) {
    return true;
  }

  struct ifreq if_request;
  memset(&if_request, 0, sizeof(if_request));
  // copy does not zero-terminate the result string, but we've memset the entire
  // struct.
  interface_name_.copy(if_request.ifr_name, IFNAMSIZ);
  if_request.ifr_mtu = mtu_;

  if (!NetdeviceIoctl(SIOCSIFMTU, reinterpret_cast<void*>(&if_request))) {
    CleanUpFileDescriptor();
    return false;
  }

  return true;
}

bool TunDevice::CheckFeatures(int tun_device_fd) {
  unsigned int actual_features;
  if (kernel_.ioctl(tun_device_fd, TUNGETFEATURES, &actual_features) != 0) {
    QUIC_PLOG(WARNING) << "Failed to TUNGETFEATURES";
    return false;
  }
  unsigned int required_features = IFF_TUN | IFF_NO_PI;
  if ((required_features & actual_features) != required_features) {
    QUIC_LOG(WARNING)
        << "Required feature does not exist. required_features: 0x" << std::hex
        << required_features << " vs actual_features: 0x" << std::hex
        << actual_features;
    return false;
  }
  return true;
}

bool TunDevice::NetdeviceIoctl(int request, void* argp) {
  int fd = kernel_.socket(AF_INET6, SOCK_DGRAM, 0);
  if (fd < 0) {
    QUIC_PLOG(WARNING) << "Failed to create AF_INET6 socket.";
    return false;
  }

  if (kernel_.ioctl(fd, request, argp) != 0) {
    QUIC_PLOG(WARNING) << "Failed ioctl request: " << request;
    kernel_.close(fd);
    return false;
  }
  kernel_.close(fd);
  return true;
}

void TunDevice::CleanUpFileDescriptor() {
  if (file_descriptor_ != kInvalidFd) {
    kernel_.close(file_descriptor_);
    file_descriptor_ = kInvalidFd;
  }
}

}  // namespace quic
