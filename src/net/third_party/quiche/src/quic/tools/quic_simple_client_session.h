// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_

#include "quic/core/http/quic_spdy_client_session.h"
#include "quic/tools/quic_simple_client_stream.h"

namespace quic {

class QuicSimpleClientSession : public QuicSpdyClientSession {
 public:
  QuicSimpleClientSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          QuicClientPushPromiseIndex* push_promise_index,
                          bool drop_response_body);
  QuicSimpleClientSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          QuicClientPushPromiseIndex* push_promise_index,
                          bool drop_response_body,
                          bool enable_web_transport);

  std::unique_ptr<QuicSpdyClientStream> CreateClientStream() override;
  bool ShouldNegotiateWebTransport() override;
  bool ShouldNegotiateHttp3Datagram() override;

 private:
  const bool drop_response_body_;
  const bool enable_web_transport_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
