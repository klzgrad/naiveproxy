// Copyright 2026 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_WEB_TRANSPORT_ONLY_CLIENT_H_
#define QUICHE_QUIC_TOOLS_WEB_TRANSPORT_ONLY_CLIENT_H_

#include <memory>
#include <string>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/proof_verifier.h"
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/http/quic_spdy_client_stream.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/io/quic_event_loop.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_connection.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/core/quic_server_id.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/tools/quic_client_base.h"
#include "quiche/quic/tools/quic_client_default_network_helper.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// WebTransportOnlyClient is a dedicated client for applications that
// are written against the webtransport::Session API. It serves as the client
// counterpart to WebTransportOnlyDispatcher.
class WebTransportOnlyClient : public QuicClientBase {
 public:
  using VisitorFactory =
      quiche::UnretainedCallback<std::unique_ptr<webtransport::SessionVisitor>(
          webtransport::Session*)>;

  WebTransportOnlyClient(
      QuicSocketAddress server_address, const QuicServerId& server_id,
      const ParsedQuicVersionVector& supported_versions,
      const QuicConfig& config, QuicEventLoop* event_loop,
      std::unique_ptr<QuicClientDefaultNetworkHelper> absl_nullable
      network_helper,
      std::unique_ptr<ProofVerifier> absl_nonnull proof_verifier,
      std::unique_ptr<SessionCache> absl_nullable session_cache);

  WebTransportOnlyClient(const WebTransportOnlyClient&) = delete;
  WebTransportOnlyClient& operator=(const WebTransportOnlyClient&) = delete;

  ~WebTransportOnlyClient() override;

  // Synchronously establishes a WebTransport session to the server at `path`,
  // configuring the session visitor by calling `visitor_factory` if the
  // connection succeeds.
  //
  // Note that the function returns when the client creates the CONNECT stream,
  // and not when the HTTP response for it is received. Doing so allows the
  // client to open streams before receiving the HTTP response. To wait until
  // the actual response is received, use OnSessionReady in the session visitor.
  absl::Status ConnectSync(std::string path, VisitorFactory visitor_factory,
                           absl::Span<const std::string> subprotocols = {},
                           const quiche::HttpHeaderBlock& extra_headers = {});

  QuicClientDefaultNetworkHelper* default_network_helper();

 protected:
  // QuicClientBase implementation.
  void InitializeSession() override;
  bool EarlyDataAccepted() override;
  bool ReceivedInchoateReject() override;
  int GetNumSentClientHellosFromSession() override;
  int GetNumReceivedServerConfigUpdatesFromSession() override;
  bool HasActiveRequests() override;

  std::unique_ptr<QuicSession> CreateQuicClientSession(
      const ParsedQuicVersionVector& supported_versions,
      QuicConnection* connection) override;

 private:
  class Session : public QuicSpdyClientSession {
   public:
    Session(const QuicConfig& config,
            const ParsedQuicVersionVector& supported_versions,
            QuicConnection* connection,
            QuicClientBase::NetworkHelper* network_helper,
            const QuicServerId& server_id,
            QuicCryptoClientConfig* crypto_config);
    ~Session() override = default;

    WebTransportHttp3VersionSet LocallySupportedWebTransportVersions()
        const override;
    HttpDatagramSupport LocalHttpDatagramSupport() override;
    void CreateContextForMultiPortPath(
        std::unique_ptr<MultiPortPathContextObserver> context_observer)
        override;
    void MigrateToMultiPortPath(
        std::unique_ptr<QuicPathValidationContext> context) override;

   private:
    QuicClientBase::NetworkHelper* network_helper_;
  };

  Session* client_session();
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_WEB_TRANSPORT_ONLY_CLIENT_H_
