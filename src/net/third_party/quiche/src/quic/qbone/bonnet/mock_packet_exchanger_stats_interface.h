// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_PACKET_EXCHANGER_STATS_INTERFACE_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_PACKET_EXCHANGER_STATS_INTERFACE_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/bonnet/tun_device_packet_exchanger.h"

namespace quic {

class MockPacketExchangerStatsInterface
    : public TunDevicePacketExchanger::StatsInterface {
 public:
  MOCK_METHOD1(OnPacketRead, void(size_t));
  MOCK_METHOD1(OnPacketWritten, void(size_t));
  MOCK_METHOD1(OnReadError, void(std::string*));
  MOCK_METHOD1(OnWriteError, void(std::string*));

  MOCK_CONST_METHOD0(PacketsRead, int64_t());
  MOCK_CONST_METHOD0(PacketsWritten, int64_t());
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_PACKET_EXCHANGER_STATS_INTERFACE_H_
