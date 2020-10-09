// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/bonnet/tun_device_packet_exchanger.h"

#include <utility>

#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"

namespace quic {

TunDevicePacketExchanger::TunDevicePacketExchanger(
    int fd,
    size_t mtu,
    KernelInterface* kernel,
    QbonePacketExchanger::Visitor* visitor,
    size_t max_pending_packets,
    StatsInterface* stats)
    : QbonePacketExchanger(visitor, max_pending_packets),
      fd_(fd),
      mtu_(mtu),
      kernel_(kernel),
      stats_(stats) {}

bool TunDevicePacketExchanger::WritePacket(const char* packet,
                                           size_t size,
                                           bool* blocked,
                                           std::string* error) {
  *blocked = false;
  if (fd_ < 0) {
    *error = quiche::QuicheStrCat("Invalid file descriptor of the TUN device: ",
                                  fd_);
    stats_->OnWriteError(error);
    return false;
  }

  int result = kernel_->write(fd_, packet, size);
  if (result == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      // The tunnel is blocked. Note that this does not mean the receive buffer
      // of a TCP connection is filled. This simply means the TUN device itself
      // is blocked on handing packets to the rest part of the kernel.
      *error =
          quiche::QuicheStrCat("Write to the TUN device was blocked: ", errno);
      *blocked = true;
      stats_->OnWriteError(error);
    }
    return false;
  }
  stats_->OnPacketWritten(result);

  return true;
}

std::unique_ptr<QuicData> TunDevicePacketExchanger::ReadPacket(
    bool* blocked,
    std::string* error) {
  *blocked = false;
  if (fd_ < 0) {
    *error = quiche::QuicheStrCat("Invalid file descriptor of the TUN device: ",
                                  fd_);
    stats_->OnReadError(error);
    return nullptr;
  }
  // Reading on a TUN device returns a packet at a time. If the packet is longer
  // than the buffer, it's truncated.
  auto read_buffer = std::make_unique<char[]>(mtu_);
  int result = kernel_->read(fd_, read_buffer.get(), mtu_);
  // Note that 0 means end of file, but we're talking about a TUN device - there
  // is no end of file. Therefore 0 also indicates error.
  if (result <= 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      *error =
          quiche::QuicheStrCat("Read from the TUN device was blocked: ", errno);
      *blocked = true;
      stats_->OnReadError(error);
    }
    return nullptr;
  }
  stats_->OnPacketRead(result);
  return std::make_unique<QuicData>(read_buffer.release(), result, true);
}

int TunDevicePacketExchanger::file_descriptor() const {
  return fd_;
}

const TunDevicePacketExchanger::StatsInterface*
TunDevicePacketExchanger::stats_interface() const {
  return stats_;
}

}  // namespace quic
