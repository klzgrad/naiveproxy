// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_TEST_TOOLS_H_
#define QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_TEST_TOOLS_H_

#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {
namespace test {

class MockWebTransportSessionVisitor : public WebTransportVisitor {
 public:
  MOCK_METHOD(void, OnSessionReady, (const spdy::Http2HeaderBlock&),
              (override));
  MOCK_METHOD(void, OnSessionClosed,
              (WebTransportSessionError, const std::string&), (override));
  MOCK_METHOD(void, OnIncomingBidirectionalStreamAvailable, (), (override));
  MOCK_METHOD(void, OnIncomingUnidirectionalStreamAvailable, (), (override));
  MOCK_METHOD(void, OnDatagramReceived, (absl::string_view), (override));
  MOCK_METHOD(void, OnCanCreateNewOutgoingBidirectionalStream, (), (override));
  MOCK_METHOD(void, OnCanCreateNewOutgoingUnidirectionalStream, (), (override));
};

class MockWebTransportStreamVisitor : public WebTransportStreamVisitor {
 public:
  MOCK_METHOD(void, OnCanRead, (), (override));
  MOCK_METHOD(void, OnCanWrite, (), (override));

  MOCK_METHOD(void, OnResetStreamReceived, (WebTransportStreamError error),
              (override));
  MOCK_METHOD(void, OnStopSendingReceived, (WebTransportStreamError error),
              (override));
  MOCK_METHOD(void, OnWriteSideInDataRecvdState, (), (override));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_TEST_TOOLS_H_
