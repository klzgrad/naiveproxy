// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_
#define NET_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_

#include "base/macros.h"
#include "net/quic/core/crypto/quic_crypto_server_config.h"
#include "net/quic/core/quic_config.h"
#include "net/quic/core/quic_packets.h"
#include "net/tools/quic/quic_simple_dispatcher.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {
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
      QuicHttpResponseCache* response_cache);

  ~MockQuicDispatcher() override;

  MOCK_METHOD3(ProcessPacket,
               void(const QuicSocketAddress& server_address,
                    const QuicSocketAddress& client_address,
                    const QuicReceivedPacket& packet));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockQuicDispatcher);
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_MOCK_QUIC_DISPATCHER_H_
