// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_TRANSPORT_TEST_TOOLS_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_TRANSPORT_TEST_TOOLS_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_client_session.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_server_session.h"

namespace quic {
namespace test {

class MockClientVisitor : public QuicTransportClientSession::ClientVisitor {
 public:
  MOCK_METHOD(void, OnSessionReady, (), (override));
  MOCK_METHOD(void, OnIncomingBidirectionalStreamAvailable, (), (override));
  MOCK_METHOD(void, OnIncomingUnidirectionalStreamAvailable, (), (override));
  MOCK_METHOD(void,
              OnDatagramReceived,
              (quiche::QuicheStringPiece),
              (override));
  MOCK_METHOD(void, OnCanCreateNewOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(void, OnCanCreateNewOutgoingUnidirectionalStream, (), (override));
};

class MockServerVisitor : public QuicTransportServerSession::ServerVisitor {
 public:
  MOCK_METHOD(bool, CheckOrigin, (url::Origin), (override));
  MOCK_METHOD(bool, ProcessPath, (const GURL&), (override));
};

class MockStreamVisitor : public QuicTransportStream::Visitor {
 public:
  MOCK_METHOD(void, OnCanRead, (), (override));
  MOCK_METHOD(void, OnFinRead, (), (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TRANSPORT_TEST_TOOLS_H_
