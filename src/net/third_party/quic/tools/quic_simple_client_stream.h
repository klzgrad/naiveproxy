// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_
#define NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_

#include "net/third_party/quic/core/http/quic_spdy_client_stream.h"

namespace quic {

class QuicSimpleClientStream : public QuicSpdyClientStream {
 public:
  QuicSimpleClientStream(QuicStreamId id,
                         QuicSpdyClientSession* session,
                         StreamType type,
                         bool drop_response_body)
      : QuicSpdyClientStream(id, session, type),
        drop_response_body_(drop_response_body),
        last_stop_sending_code_(0) {}
  void OnBodyAvailable() override;
  void OnStopSending(uint16_t code) override;

  uint16_t last_stop_sending_code() { return last_stop_sending_code_; }

 private:
  const bool drop_response_body_;
  // Application code value that was in the most recently received
  // STOP_SENDING frame for this stream.
  uint16_t last_stop_sending_code_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_
