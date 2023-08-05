// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_PACKET_EXCHANGER_STATS_INTERFACE_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_PACKET_EXCHANGER_STATS_INTERFACE_H_

#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/bonnet/tun_device_packet_exchanger.h"

namespace quic {

class MockPacketExchangerStatsInterface
    : public TunDevicePacketExchanger::StatsInterface {
 public:
  MOCK_METHOD(void, OnPacketRead, (size_t), (override));
  MOCK_METHOD(void, OnPacketWritten, (size_t), (override));
  MOCK_METHOD(void, OnReadError, (std::string*), (override));
  MOCK_METHOD(void, OnWriteError, (std::string*), (override));

  MOCK_METHOD(int64_t, PacketsRead, (), (const, override));
  MOCK_METHOD(int64_t, PacketsWritten, (), (const, override));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_PACKET_EXCHANGER_STATS_INTERFACE_H_
