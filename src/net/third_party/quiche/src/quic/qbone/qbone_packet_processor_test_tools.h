// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_TEST_TOOLS_H_
#define QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_TEST_TOOLS_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/qbone_packet_processor.h"

namespace quic {

class MockPacketProcessorOutput : public QbonePacketProcessor::OutputInterface {
 public:
  MockPacketProcessorOutput() {}

  MOCK_METHOD1(SendPacketToClient, void(QuicStringPiece));
  MOCK_METHOD1(SendPacketToNetwork, void(QuicStringPiece));
};

class MockPacketProcessorStats : public QbonePacketProcessor::StatsInterface {
 public:
  MockPacketProcessorStats() {}

  MOCK_METHOD1(OnPacketForwarded, void(QbonePacketProcessor::Direction));
  MOCK_METHOD1(OnPacketDroppedSilently, void(QbonePacketProcessor::Direction));
  MOCK_METHOD1(OnPacketDroppedWithIcmp, void(QbonePacketProcessor::Direction));
  MOCK_METHOD1(OnPacketDroppedWithTcpReset,
               void(QbonePacketProcessor::Direction));
  MOCK_METHOD1(OnPacketDeferred, void(QbonePacketProcessor::Direction));
};

string PrependIPv6HeaderForTest(const string& body, int hops);

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_TEST_TOOLS_H_
