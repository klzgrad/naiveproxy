// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_client_stream.h"

namespace quic {

class QuicSimpleClientStream : public QuicSpdyClientStream {
 public:
  QuicSimpleClientStream(QuicStreamId id,
                         QuicSpdyClientSession* session,
                         StreamType type,
                         bool drop_response_body)
      : QuicSpdyClientStream(id, session, type),
        drop_response_body_(drop_response_body) {}
  void OnBodyAvailable() override;

 private:
  const bool drop_response_body_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_STREAM_H_
