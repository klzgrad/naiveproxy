// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_

#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/tools/quic_client_base.h"
#include "quiche/quic/tools/quic_simple_client_stream.h"

namespace quic {

class QuicSimpleClientSession : public QuicSpdyClientSession {
 public:
  QuicSimpleClientSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          QuicClientBase::NetworkHelper* network_helper,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          QuicClientPushPromiseIndex* push_promise_index,
                          bool drop_response_body, bool enable_web_transport);

  QuicSimpleClientSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          QuicSession::Visitor* visitor,
                          QuicClientBase::NetworkHelper* network_helper,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          QuicClientPushPromiseIndex* push_promise_index,
                          bool drop_response_body, bool enable_web_transport);

  std::unique_ptr<QuicSpdyClientStream> CreateClientStream() override;
  bool ShouldNegotiateWebTransport() override;
  HttpDatagramSupport LocalHttpDatagramSupport() override;
  std::unique_ptr<QuicPathValidationContext> CreateContextForMultiPortPath()
      override;
  bool drop_response_body() const { return drop_response_body_; }

 private:
  QuicClientBase::NetworkHelper* network_helper_;
  const bool drop_response_body_;
  const bool enable_web_transport_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
