// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_

#include "base/macros.h"
#include "net/third_party/quic/core/crypto/quic_crypto_server_config.h"
#include "net/third_party/quic/core/quic_config.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/tools/quic_simple_dispatcher.h"
#include "net/third_party/quic/tools/quic_simple_server_backend.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace quic {
namespace test {

class MockQuicDispatcher : public QuicSimpleDispatcher {
 public:
  MockQuicDispatcher(
      const QuicConfig& config,
      const QuicCryptoServerConfig* crypto_config,
      QuicVersionManager* version_manager,
      std::unique_ptr<QuicConnectionHelperInterface> helper,
      std::unique_ptr<QuicCryptoServerStream::Helper> session_helper,
      std::unique_ptr<QuicAlarmFactory> alarm_factory,
      QuicSimpleServerBackend* quic_simple_server_backend);
  MockQuicDispatcher(const MockQuicDispatcher&) = delete;
  MockQuicDispatcher& operator=(const MockQuicDispatcher&) = delete;

  ~MockQuicDispatcher() override;

  MOCK_METHOD3(ProcessPacket,
               void(const QuicSocketAddress& server_address,
                    const QuicSocketAddress& client_address,
                    const QuicReceivedPacket& packet));
};

}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_
