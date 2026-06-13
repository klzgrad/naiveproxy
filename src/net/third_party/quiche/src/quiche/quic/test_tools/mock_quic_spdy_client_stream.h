// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SPDY_CLIENT_STREAM_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SPDY_CLIENT_STREAM_H_

#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class MockQuicSpdyClientStream : public QuicSpdyClientStream {
 public:
  MockQuicSpdyClientStream(QuicStreamId id, QuicSpdyClientSession* session,
                           StreamType type);
  ~MockQuicSpdyClientStream() override;

  MOCK_METHOD(void, OnStreamFrame, (const QuicStreamFrame& frame), (override));
  MOCK_METHOD(void, OnDataAvailable, (), (override));
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SPDY_CLIENT_STREAM_H_
