// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_

#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_time_wait_list_manager.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class MockQuicSessionVisitor : public QuicTimeWaitListManager::Visitor {
 public:
  MockQuicSessionVisitor();
  MockQuicSessionVisitor(const MockQuicSessionVisitor&) = delete;
  MockQuicSessionVisitor& operator=(const MockQuicSessionVisitor&) = delete;
  ~MockQuicSessionVisitor() override;
  MOCK_METHOD(void, OnConnectionClosed,
              (QuicConnectionId connection_id, QuicErrorCode error,
               const std::string& error_details, ConnectionCloseSource source),
              (override));
  MOCK_METHOD(void, OnWriteBlocked, (QuicBlockedWriterInterface*), (override));
  MOCK_METHOD(void, OnRstStreamReceived, (const QuicRstStreamFrame& frame),
              (override));
  MOCK_METHOD(void, OnStopSendingReceived, (const QuicStopSendingFrame& frame),
              (override));
  MOCK_METHOD(bool, TryAddNewConnectionId,
              (const QuicConnectionId& server_connection_id,
               const QuicConnectionId& new_connection_id),
              (override));
  MOCK_METHOD(void, OnConnectionIdRetired,
              (const quic::QuicConnectionId& server_connection_id), (override));
  MOCK_METHOD(void, OnConnectionAddedToTimeWaitList,
              (QuicConnectionId connection_id), (override));
  MOCK_METHOD(void, OnServerPreferredAddressAvailable,
              (const QuicSocketAddress& server_preferred_address), (override));
};

class MockQuicCryptoServerStreamHelper
    : public QuicCryptoServerStreamBase::Helper {
 public:
  MockQuicCryptoServerStreamHelper();
  MockQuicCryptoServerStreamHelper(const MockQuicCryptoServerStreamHelper&) =
      delete;
  MockQuicCryptoServerStreamHelper& operator=(
      const MockQuicCryptoServerStreamHelper&) = delete;
  ~MockQuicCryptoServerStreamHelper() override;
  MOCK_METHOD(bool, CanAcceptClientHello,
              (const CryptoHandshakeMessage& message,
               const QuicSocketAddress& client_address,
               const QuicSocketAddress& peer_address,
               const QuicSocketAddress& self_address, std::string*),
              (const, override));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SESSION_VISITOR_H_
