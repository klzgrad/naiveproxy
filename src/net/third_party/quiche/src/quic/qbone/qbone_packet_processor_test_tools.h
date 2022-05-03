// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_TEST_TOOLS_H_
#define QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_TEST_TOOLS_H_

#include "absl/strings/string_view.h"
#include "quic/platform/api/quic_test.h"
#include "quic/qbone/qbone_packet_processor.h"

namespace quic {

class MockPacketProcessorOutput : public QbonePacketProcessor::OutputInterface {
 public:
  MockPacketProcessorOutput() {}

  MOCK_METHOD(void, SendPacketToClient, (absl::string_view), (override));
  MOCK_METHOD(void, SendPacketToNetwork, (absl::string_view), (override));
};

class MockPacketProcessorStats : public QbonePacketProcessor::StatsInterface {
 public:
  MockPacketProcessorStats() {}

  MOCK_METHOD(void,
              OnPacketForwarded,
              (QbonePacketProcessor::Direction),
              (override));
  MOCK_METHOD(void,
              OnPacketDroppedSilently,
              (QbonePacketProcessor::Direction),
              (override));
  MOCK_METHOD(void,
              OnPacketDroppedWithIcmp,
              (QbonePacketProcessor::Direction),
              (override));
  MOCK_METHOD(void,
              OnPacketDroppedWithTcpReset,
              (QbonePacketProcessor::Direction),
              (override));
  MOCK_METHOD(void,
              OnPacketDeferred,
              (QbonePacketProcessor::Direction),
              (override));
};

std::string PrependIPv6HeaderForTest(const std::string& body, int hops);

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_PACKET_PROCESSOR_TEST_TOOLS_H_
