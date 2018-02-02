// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/mock_quic_dispatcher.h"

#include "net/quic/test_tools/quic_test_utils.h"

namespace net {
namespace test {

MockQuicDispatcher::MockQuicDispatcher(
    const QuicConfig& config,
    const QuicCryptoServerConfig* crypto_config,
    QuicVersionManager* version_manager,
    std::unique_ptr<QuicConnectionHelperInterface> helper,
    std::unique_ptr<QuicCryptoServerStream::Helper> session_helper,
    std::unique_ptr<QuicAlarmFactory> alarm_factory,
    QuicHttpResponseCache* response_cache)
    : QuicSimpleDispatcher(config,
                           crypto_config,
                           version_manager,
                           std::move(helper),
                           std::move(session_helper),
                           std::move(alarm_factory),
                           response_cache) {}

MockQuicDispatcher::~MockQuicDispatcher() {}

}  // namespace test
}  // namespace net
