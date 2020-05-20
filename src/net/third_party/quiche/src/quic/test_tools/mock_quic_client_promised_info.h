// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_CLIENT_PROMISED_INFO_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_CLIENT_PROMISED_INFO_H_

#include <string>

#include "net/third_party/quiche/src/quic/core/http/quic_client_promised_info.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class MockQuicClientPromisedInfo : public QuicClientPromisedInfo {
 public:
  MockQuicClientPromisedInfo(QuicSpdyClientSessionBase* session,
                             QuicStreamId id,
                             std::string url);
  ~MockQuicClientPromisedInfo() override;

  MOCK_METHOD2(HandleClientRequest,
               QuicAsyncStatus(const spdy::SpdyHeaderBlock& headers,
                               QuicClientPushPromiseIndex::Delegate* delegate));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_CLIENT_PROMISED_INFO_H_
