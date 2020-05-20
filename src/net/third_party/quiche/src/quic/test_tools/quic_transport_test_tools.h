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
  MOCK_METHOD0(OnSessionReady, void());
  MOCK_METHOD0(OnIncomingBidirectionalStreamAvailable, void());
  MOCK_METHOD0(OnIncomingUnidirectionalStreamAvailable, void());
  MOCK_METHOD1(OnDatagramReceived, void(quiche::QuicheStringPiece));
  MOCK_METHOD0(OnCanCreateNewOutgoingBidirectionalStream, void());
  MOCK_METHOD0(OnCanCreateNewOutgoingUnidirectionalStream, void());
};

class MockServerVisitor : public QuicTransportServerSession::ServerVisitor {
 public:
  MOCK_METHOD1(CheckOrigin, bool(url::Origin));
  MOCK_METHOD1(ProcessPath, bool(const GURL&));
};

class MockStreamVisitor : public QuicTransportStream::Visitor {
 public:
  MOCK_METHOD0(OnCanRead, void());
  MOCK_METHOD0(OnFinRead, void());
  MOCK_METHOD0(OnCanWrite, void());
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_TRANSPORT_TEST_TOOLS_H_
