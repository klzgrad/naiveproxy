// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/qbone/bonnet/tun_device.h"

#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <ios>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/quic/qbone/platform/kernel_interface.h"

ABSL_FLAG(std::string, qbone_client_tun_device_path, "/dev/net/tun",
          "The path to the QBONE client's TUN device.");

namespace quic {

const int kInvalidFd = -1;

TunTapDevice::TunTapDevice(const std::string& interface_name, int mtu,
                           bool persist, bool setup_tun, bool is_tap,
                           KernelInterface* kernel)
    : interface_name_(interface_name),
      mtu_(mtu),
      persist_(persist),
      setup_tun_(setup_tun),
      is_tap_(is_tap),
      file_descriptor_(kInvalidFd),
      kernel_(*kernel) {}

TunTapDevice::~TunTapDevice() {
  if (!persist_) {
    Down();
  }
  CloseDevice();
}

bool TunTapDevice::Init() {
  if (interface_name_.empty() || interface_name_.size() >= IFNAMSIZ) {
    QUIC_BUG(quic_bug_10995_1)
        << "interface_name must be nonempty and shorter than " << IFNAMSIZ;
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
bool TunTapDevice::Up() {
  if (!setup_tun_) {
    return true;
  }
  struct ifreq if_request;
  memset(&if_request, 0, sizeof(if_request));
  // copy does not zero-terminate the result string, but we've memset the
  // entire struct.
  interface_name_.copy(if_request.ifr_name, IFNAMSIZ);
  if_request.ifr_flags = IFF_UP;

  return NetdeviceIoctl(SIOCSIFFLAGS, reinterpret_cast<void*>(&if_request));
}

// TODO(pengg): might be better to use netlink socket, once we have a library to
// use
bool TunTapDevice::Down() {
  if (!setup_tun_) {
    return true;
  }
  struct ifreq if_request;
  memset(&if_request, 0, sizeof(if_request));
  // copy does not zero-terminate the result string, but we've memset the
  // entire struct.
  interface_name_.copy(if_request.ifr_name, IFNAMSIZ);
  if_request.ifr_flags = 0;

  return NetdeviceIoctl(SIOCSIFFLAGS, reinterpret_cast<void*>(&if_request));
}

int TunTapDevice::GetFileDescriptor() const { return file_descriptor_; }

bool TunTapDevice::OpenDevice() {
  if (file_descriptor_ != kInvalidFd) {
    CloseDevice();
  }

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
  if_request.ifr_flags = IFF_MULTI_QUEUE | IFF_NO_PI;
  if (is_tap_) {
    if_request.ifr_flags |= IFF_TAP;
  } else {
    if_request.ifr_flags |= IFF_TUN;
  }

  // When the device is running with IFF_MULTI_QUEUE set, each call to open will
  // create a queue which can be used to read/write packets from/to the device.
  bool successfully_opened = false;
  auto cleanup = absl::MakeCleanup([this, &successfully_opened]() {
    if (!successfully_opened) {
      CloseDevice();
    }
  });

  const std::string tun_device_path =
      absl::GetFlag(FLAGS_qbone_client_tun_device_path);
  int fd = kernel_.open(tun_device_path.c_str(), O_RDWR);
  if (fd < 0) {
    QUIC_PLOG(WARNING) << "Failed to open " << tun_device_path;
    return successfully_opened;
  }
  file_descriptor_ = fd;
  if (!CheckFeatures(fd)) {
    return successfully_opened;
  }

  if (kernel_.ioctl(fd, TUNSETIFF, reinterpret_cast<void*>(&if_request)) != 0) {
    QUIC_PLOG(WARNING) << "Failed to TUNSETIFF on fd(" << fd << ")";
    return successfully_opened;
  }

  if (kernel_.ioctl(
          fd, TUNSETPERSIST,
          persist_ ? reinterpret_cast<void*>(&if_request) : nullptr) != 0) {
    QUIC_PLOG(WARNING) << "Failed to TUNSETPERSIST on fd(" << fd << ")";
    return successfully_opened;
  }

  successfully_opened = true;
  return successfully_opened;
}

// TODO(pengg): might be better to use netlink socket, once we have a library to
// use
bool TunTapDevice::ConfigureInterface() {
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
    CloseDevice();
    return false;
  }

  return true;
}

bool TunTapDevice::CheckFeatures(int tun_device_fd) {
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

bool TunTapDevice::NetdeviceIoctl(int request, void* argp) {
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

void TunTapDevice::CloseDevice() {
  if (file_descriptor_ != kInvalidFd) {
    kernel_.close(file_descriptor_);
    file_descriptor_ = kInvalidFd;
  }
}

}  // namespace quic
