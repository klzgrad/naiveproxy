// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_BONNET_MOCK_QBONE_TUNNEL_H_
#define QUICHE_QUIC_QBONE_BONNET_MOCK_QBONE_TUNNEL_H_

#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/bonnet/qbone_tunnel_interface.h"

namespace quic {

class MockQboneTunnel : public QboneTunnelInterface {
 public:
  MockQboneTunnel() = default;

  MOCK_METHOD(bool, WaitForEvents, (), (override));

  MOCK_METHOD(void, Wake, (), (override));

  MOCK_METHOD(void, ResetTunnel, (), (override));

  MOCK_METHOD(State, Disconnect, (), (override));

  MOCK_METHOD(void, OnControlRequest, (const quic::QboneClientRequest&),
              (override));

  MOCK_METHOD(void, OnControlError, (), (override));

  MOCK_METHOD(bool, AwaitConnection, ());

  MOCK_METHOD(std::string, StateToString, (State), (override));

  MOCK_METHOD(quic::QboneClient*, client, (), (override));

  MOCK_METHOD(bool, use_quarantine_mode, (), (const, override));

  MOCK_METHOD(bool, routes_set, (), (const, override));

  MOCK_METHOD(State, state, ());

  MOCK_METHOD(std::string, HealthString, ());

  MOCK_METHOD(std::string, ServerRegionString, ());
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_BONNET_MOCK_QBONE_TUNNEL_H_
