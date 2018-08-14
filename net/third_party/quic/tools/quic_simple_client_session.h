// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
#define NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_

#include "net/third_party/quic/core/http/quic_spdy_client_session.h"
#include "net/third_party/quic/tools/quic_simple_client_stream.h"

namespace quic {

class QuicSimpleClientSession : public QuicSpdyClientSession {
 public:
  QuicSimpleClientSession(const QuicConfig& config,
                          QuicConnection* connection,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          QuicClientPushPromiseIndex* push_promise_index,
                          bool drop_response_body)
      : QuicSpdyClientSession(config,
                              connection,
                              server_id,
                              crypto_config,
                              push_promise_index),
        drop_response_body_(drop_response_body) {}

  std::unique_ptr<QuicSpdyClientStream> CreateClientStream() override;

 private:
  const bool drop_response_body_;
};

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
