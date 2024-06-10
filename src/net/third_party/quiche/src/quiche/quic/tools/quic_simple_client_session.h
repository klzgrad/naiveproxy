// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_

#include <functional>
#include <utility>

#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/tools/quic_client_base.h"
#include "quiche/quic/tools/quic_simple_client_stream.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

class QuicSimpleClientSession : public QuicSpdyClientSession {
 public:
  QuicSimpleClientSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          QuicClientBase::NetworkHelper* network_helper,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          bool drop_response_body, bool enable_web_transport);

  QuicSimpleClientSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          QuicSession::Visitor* visitor,
                          QuicClientBase::NetworkHelper* network_helper,
                          const QuicServerId& server_id,
                          QuicCryptoClientConfig* crypto_config,
                          bool drop_response_body, bool enable_web_transport);

  std::unique_ptr<QuicSpdyClientStream> CreateClientStream() override;
  WebTransportHttp3VersionSet LocallySupportedWebTransportVersions()
      const override;
  HttpDatagramSupport LocalHttpDatagramSupport() override;
  void CreateContextForMultiPortPath(
      std::unique_ptr<MultiPortPathContextObserver> context_observer) override;
  void MigrateToMultiPortPath(
      std::unique_ptr<QuicPathValidationContext> context) override;
  bool drop_response_body() const { return drop_response_body_; }

  void set_on_interim_headers(
      quiche::MultiUseCallback<void(const spdy::Http2HeaderBlock&)>
          on_interim_headers) {
    on_interim_headers_ = std::move(on_interim_headers);
  }

 private:
  quiche::MultiUseCallback<void(const spdy::Http2HeaderBlock&)>
      on_interim_headers_;
  QuicClientBase::NetworkHelper* network_helper_;
  const bool drop_response_body_;
  const bool enable_web_transport_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_CLIENT_SESSION_H_
