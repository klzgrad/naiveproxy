// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_
#define NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_

#include "base/macros.h"
#include "net/third_party/quic/core/quic_crypto_server_stream.h"
#include "net/third_party/quic/core/quic_time_wait_list_manager.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace quic {
namespace test {

class MockQuicSessionVisitor : public QuicTimeWaitListManager::Visitor {
 public:
  MockQuicSessionVisitor();
  MockQuicSessionVisitor(const MockQuicSessionVisitor&) = delete;
  MockQuicSessionVisitor& operator=(const MockQuicSessionVisitor&) = delete;
  ~MockQuicSessionVisitor() override;
  MOCK_METHOD3(OnConnectionClosed,
               void(QuicConnectionId connection_id,
                    QuicErrorCode error,
                    const QuicString& error_details));
  MOCK_METHOD1(OnWriteBlocked,
               void(QuicBlockedWriterInterface* blocked_writer));
  MOCK_METHOD1(OnRstStreamReceived, void(const QuicRstStreamFrame& frame));
  MOCK_METHOD1(OnConnectionAddedToTimeWaitList,
               void(QuicConnectionId connection_id));
};

class MockQuicCryptoServerStreamHelper : public QuicCryptoServerStream::Helper {
 public:
  MockQuicCryptoServerStreamHelper();
  MockQuicCryptoServerStreamHelper(const MockQuicCryptoServerStreamHelper&) =
      delete;
  MockQuicCryptoServerStreamHelper& operator=(
      const MockQuicCryptoServerStreamHelper&) = delete;
  ~MockQuicCryptoServerStreamHelper() override;
  MOCK_CONST_METHOD1(GenerateConnectionIdForReject,
                     QuicConnectionId(QuicConnectionId connection_id));
  MOCK_CONST_METHOD5(CanAcceptClientHello,
                     bool(const CryptoHandshakeMessage& message,
                          const QuicSocketAddress& client_address,
                          const QuicSocketAddress& peer_address,
                          const QuicSocketAddress& self_address,
                          QuicString* error_details));
};

}  // namespace test
}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_
