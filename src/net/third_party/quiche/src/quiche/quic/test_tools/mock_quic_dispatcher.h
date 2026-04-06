// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_

#include "quiche/quic/core/crypto/quic_crypto_server_config.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/tools/quic_simple_dispatcher.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"

namespace quic {
namespace test {

class MockQuicDispatcher : public QuicSimpleDispatcher {
 public:
  MockQuicDispatcher(
      const QuicConfig* config, const QuicCryptoServerConfig* crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      QuicSimpleServerBackend* quic_simple_server_backend,
      ConnectionIdGeneratorInterface& generator);
  MockQuicDispatcher(const MockQuicDispatcher&) = delete;
  MockQuicDispatcher& operator=(const MockQuicDispatcher&) = delete;

  ~MockQuicDispatcher() override;

  MOCK_METHOD(void, ProcessPacket,
              (const QuicSocketAddress& server_address,
               const QuicSocketAddress& client_address,
               const QuicReceivedPacket& packet),
              (override));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_
