// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_TEST_TOOLS_MOCK_QUIC_CLIENT_PROMISED_INFO_H_
#define NET_QUIC_TEST_TOOLS_MOCK_QUIC_CLIENT_PROMISED_INFO_H_

#include <string>

#include "base/macros.h"
#include "net/quic/core/quic_client_promised_info.h"
#include "net/quic/core/quic_packets.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {
namespace test {

class MockQuicClientPromisedInfo : public QuicClientPromisedInfo {
 public:
  MockQuicClientPromisedInfo(QuicSpdyClientSessionBase* session,
                             QuicStreamId id,
                             std::string url);
  ~MockQuicClientPromisedInfo() override;

  MOCK_METHOD2(HandleClientRequest,
               QuicAsyncStatus(const SpdyHeaderBlock& headers,
                               QuicClientPushPromiseIndex::Delegate* delegate));
};

}  // namespace test
}  // namespace net

#endif  // NET_QUIC_TEST_TOOLS_MOCK_QUIC_CLIENT_PROMISED_INFO_H_
