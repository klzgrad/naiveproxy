// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SPDY_CLIENT_STREAM_H_
#define QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SPDY_CLIENT_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/quic_header_list.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"

namespace quic {
namespace test {

class MockQuicSpdyClientStream : public QuicSpdyClientStream {
 public:
  MockQuicSpdyClientStream(QuicStreamId id,
                           QuicSpdyClientSession* session,
                           StreamType type);
  ~MockQuicSpdyClientStream() override;

  MOCK_METHOD1(OnStreamFrame, void(const QuicStreamFrame& frame));
  MOCK_METHOD3(OnPromiseHeaderList,
               void(QuicStreamId promised_stream_id,
                    size_t frame_len,
                    const QuicHeaderList& list));
  MOCK_METHOD0(OnDataAvailable, void());
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_MOCK_QUIC_SPDY_CLIENT_STREAM_H_
