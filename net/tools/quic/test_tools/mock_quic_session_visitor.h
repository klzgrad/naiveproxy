// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_
#define NET_TOOLS_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_

#include "base/macros.h"
#include "net/quic/core/quic_crypto_server_stream.h"
#include "net/tools/quic/quic_time_wait_list_manager.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {
namespace test {

class MockQuicSessionVisitor : public QuicTimeWaitListManager::Visitor {
 public:
  MockQuicSessionVisitor();
  ~MockQuicSessionVisitor() override;
  MOCK_METHOD3(OnConnectionClosed,
               void(QuicConnectionId connection_id,
                    QuicErrorCode error,
                    const std::string& error_details));
  MOCK_METHOD1(OnWriteBlocked,
               void(QuicBlockedWriterInterface* blocked_writer));
  MOCK_METHOD1(OnRstStreamReceived, void(const QuicRstStreamFrame& frame));
  MOCK_METHOD1(OnConnectionAddedToTimeWaitList,
               void(QuicConnectionId connection_id));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockQuicSessionVisitor);
};

class MockQuicCryptoServerStreamHelper : public QuicCryptoServerStream::Helper {
 public:
  MockQuicCryptoServerStreamHelper();
  ~MockQuicCryptoServerStreamHelper() override;
  MOCK_CONST_METHOD1(GenerateConnectionIdForReject,
                     QuicConnectionId(QuicConnectionId connection_id));
  MOCK_CONST_METHOD3(CanAcceptClientHello,
                     bool(const CryptoHandshakeMessage& message,
                          const QuicSocketAddress& self_address,
                          std::string* error_details));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockQuicCryptoServerStreamHelper);
};

}  // namespace test
}  // namespace net

#endif  // NET_TOOLS_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_
