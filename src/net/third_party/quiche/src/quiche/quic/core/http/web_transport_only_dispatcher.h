// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_ONLY_DISPATCHER_H_
#define QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_ONLY_DISPATCHER_H_

#include <memory>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/connection_id_generator.h"
#include "quiche/quic/core/http/web_transport_only_server_session.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// Parameters defining behavior of WebTransportOnlyDispatcher.
struct QUICHE_EXPORT WebTransportOnlyDispatcherParameters {
  // Returns the application-specific handler for an incoming session. If a
  // nullptr is returned, the session will not be created.
  WebTransportHandlerFactoryCallback handler_factory =
      +[](webtransport::Session* absl_nonnull,
          const WebTransportIncomingRequestDetails&) {
        return absl::UnimplementedError("Backend not configured");
      };

  // Selects one of the provided subprotocols to be used for the incoming
  // session. If the value returned is outside of the [0, subprotocols.size())
  // range, the negotiation is assumed to be unsuccessful. For raw QUIC, this is
  // a fatal error. For WebTransport over HTTP/3, an empty subprotocol is
  // provided to the resulting session.
  WebTransportSelectSubprotocolCallback subprotocol_callback =
      +[](absl::Span<const absl::string_view> /*subprotocols*/) { return -1; };
};

// WebTransportOnlyDispatcher is a dedicated dispatcher for applications that
// are written against the webtransport::Session API.
class QUICHE_EXPORT WebTransportOnlyDispatcher : public QuicDispatcher {
 public:
  using QuicDispatcher::QuicDispatcher;

  WebTransportOnlyDispatcherParameters& parameters() { return parameters_; }

  // QuicDispatcher implementation.
  std::unique_ptr<QuicSession> CreateQuicSession(
      QuicConnectionId server_connection_id,
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address, absl::string_view alpn,
      const ParsedQuicVersion& version, const ParsedClientHello& parsed_chlo,
      ConnectionIdGeneratorInterface& connection_id_generator) override;

 private:
  WebTransportOnlyDispatcherParameters parameters_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_ONLY_DISPATCHER_H_
