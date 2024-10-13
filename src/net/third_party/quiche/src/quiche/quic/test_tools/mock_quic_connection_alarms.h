// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_CONNECTION_ALARMS_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_CONNECTION_ALARMS_H_

#include "quiche/quic/core/quic_connection_alarms.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace quic::test {

class MockConnectionAlarmsDelegate : public QuicConnectionAlarmsDelegate {
 public:
  MOCK_METHOD(void, OnSendAlarm, (), (override));
  MOCK_METHOD(void, OnAckAlarm, (), (override));
  MOCK_METHOD(void, OnRetransmissionAlarm, (), (override));
  MOCK_METHOD(void, OnMtuDiscoveryAlarm, (), (override));
  MOCK_METHOD(void, OnProcessUndecryptablePacketsAlarm, (), (override));
  MOCK_METHOD(void, OnDiscardPreviousOneRttKeysAlarm, (), (override));
  MOCK_METHOD(void, OnDiscardZeroRttDecryptionKeysAlarm, (), (override));
  MOCK_METHOD(void, MaybeProbeMultiPortPath, (), (override));
  MOCK_METHOD(void, OnIdleDetectorAlarm, (), (override));
  MOCK_METHOD(void, OnNetworkBlackholeDetectorAlarm, (), (override));
  MOCK_METHOD(void, OnPingAlarm, (), (override));

  QuicConnectionContext* context() override { return nullptr; }
};

}  // namespace quic::test

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_CONNECTION_ALARMS_H_
