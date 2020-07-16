// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/mock_quic_dispatcher.h"

#include "net/third_party/quiche/src/quic/test_tools/quic_test_utils.h"

namespace quic {
namespace test {

MockQuicDispatcher::MockQuicDispatcher(
    const QuicConfig* config,
    const QuicCryptoServerConfig* crypto_config,
    QuicVersionManager* version_manager,
    std::unique_ptr<QuicConnectionHelperInterface> helper,
    std::unique_ptr<QuicCryptoServerStreamBase::Helper> session_helper,
    std::unique_ptr<QuicAlarmFactory> alarm_factory,
    QuicSimpleServerBackend* quic_simple_server_backend)
    : QuicSimpleDispatcher(config,
                           crypto_config,
                           version_manager,
                           std::move(helper),
                           std::move(session_helper),
                           std::move(alarm_factory),
                           quic_simple_server_backend,
                           kQuicDefaultConnectionIdLength) {}

MockQuicDispatcher::~MockQuicDispatcher() {}

}  // namespace test
}  // namespace quic
