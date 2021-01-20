// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_PACKET_EXCHANGER_H_
#define QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_PACKET_EXCHANGER_H_

#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/qbone/platform/kernel_interface.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_client_interface.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_packet_exchanger.h"

namespace quic {

class TunDevicePacketExchanger : public QbonePacketExchanger {
 public:
  class StatsInterface {
   public:
    StatsInterface() = default;

    StatsInterface(const StatsInterface&) = delete;
    StatsInterface& operator=(const StatsInterface&) = delete;

    StatsInterface(StatsInterface&&) = delete;
    StatsInterface& operator=(StatsInterface&&) = delete;

    virtual ~StatsInterface() = default;

    virtual void OnPacketRead(size_t count) = 0;
    virtual void OnPacketWritten(size_t count) = 0;
    virtual void OnReadError(std::string* error) = 0;
    virtual void OnWriteError(std::string* error) = 0;

    ABSL_MUST_USE_RESULT virtual int64_t PacketsRead() const = 0;
    ABSL_MUST_USE_RESULT virtual int64_t PacketsWritten() const = 0;
  };

  // |fd| is a open file descriptor on a TUN device that's opened for both read
  // and write.
  // |mtu| is the mtu of the TUN device.
  // |kernel| is not owned but should out live objects of this class.
  // |visitor| is not owned but should out live objects of this class.
  // |max_pending_packets| controls the number of packets to be queued should
  // the TUN device become blocked.
  // |stats| is notified about packet read/write statistics. It is not owned,
  // but should outlive objects of this class.
  TunDevicePacketExchanger(int fd,
                           size_t mtu,
                           KernelInterface* kernel,
                           QbonePacketExchanger::Visitor* visitor,
                           size_t max_pending_packets,
                           StatsInterface* stats);

  ABSL_MUST_USE_RESULT int file_descriptor() const;

  ABSL_MUST_USE_RESULT const StatsInterface* stats_interface() const;

 private:
  // From QbonePacketExchanger.
  std::unique_ptr<QuicData> ReadPacket(bool* blocked,
                                       std::string* error) override;

  // From QbonePacketExchanger.
  bool WritePacket(const char* packet,
                   size_t size,
                   bool* blocked,
                   std::string* error) override;

  int fd_ = -1;
  size_t mtu_;
  KernelInterface* kernel_;

  StatsInterface* stats_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_TUN_DEVICE_PACKET_EXCHANGER_H_
