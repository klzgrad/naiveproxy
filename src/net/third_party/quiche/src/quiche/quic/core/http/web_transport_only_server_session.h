// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_ONLY_SERVER_SESSION_H_
#define QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_ONLY_SERVER_SESSION_H_

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/core/http/quic_spdy_server_stream_base.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/quic_spdy_stream.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// Details about an incoming WebTransport request that are provided to the
// application.
struct QUICHE_EXPORT WebTransportIncomingRequestDetails {
  // HTTP request headers received for the WebTransport CONNECT request.
  quiche::HttpHeaderBlock headers;
  // Subprotocol selected during the negotiation phase, if any.
  std::string subprotocol;
};

// Application-provided response for the incoming WebTransport session.
struct QUICHE_EXPORT WebTransportConnectResponse {
  // The visitor handling the WebTransport request.  Must be non-null.
  std::unique_ptr<webtransport::SessionVisitor> visitor;
  // Additional response headers added to the WebTransport CONNECT response.
  quiche::HttpHeaderBlock headers;
};

using WebTransportSelectSubprotocolCallback =
    quiche::MultiUseCallback<int(absl::Span<const absl::string_view>)>;

// Important note: the callback MUST NOT access `session` in any way other than
// storing it, as it may be not fully initialized until later.
using WebTransportHandlerFactoryCallback =
    quiche::MultiUseCallback<absl::StatusOr<WebTransportConnectResponse>(
        webtransport::Session* absl_nonnull session,
        const WebTransportIncomingRequestDetails& details)>;

// WebTransportOnlyServerSession is an HTTP/3 session that only accepts incoming
// WebTransport requests.  All other requests are rejected with the HTTP error
// 405 Method Not Allowed.
//
// WebTransportOnlyServerSession takes ownership of all underlying connections.
class QUICHE_EXPORT WebTransportOnlyServerSession
    : public QuicServerSessionBase {
 public:
  using QuicServerSessionBase::QuicServerSessionBase;
  ~WebTransportOnlyServerSession() override;

  void SetHandlerFactory(WebTransportHandlerFactoryCallback factory) {
    handler_factory_ = std::move(factory);
  }
  void SetSubprotocolCallback(WebTransportSelectSubprotocolCallback callback) {
    subprotocol_callback_ = std::move(callback);
  }

  void Initialize() override;
  std::unique_ptr<QuicCryptoServerStreamBase> CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override;
  bool OnSettingsFrame(const SettingsFrame& frame) override;

  QuicSpdyStream* CreateIncomingStream(QuicStreamId id) override;
  QuicSpdyStream* CreateIncomingStream(PendingStream* pending) override;
  QuicSpdyStream* CreateOutgoingBidirectionalStream() override;
  QuicStream* ProcessBidirectionalPendingStream(
      PendingStream* pending) override;

  WebTransportHttp3VersionSet LocallySupportedWebTransportVersions()
      const override {
    return kDefaultSupportedWebTransportVersions;
  }
  HttpDatagramSupport LocalHttpDatagramSupport() override {
    return HttpDatagramSupport::kRfcAndDraft04;
  }

 private:
  class Stream : public QuicSpdyServerStreamBase {
   public:
    using QuicSpdyServerStreamBase::QuicSpdyServerStreamBase;

    void OnBodyAvailable() override;
    void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                  const QuicHeaderList& header_list) override;
    void SendErrorResponse(int code);
    std::string SelectSubprotocolResponse(
        absl::string_view client_header_value);
  };

  WebTransportHandlerFactoryCallback handler_factory_;
  WebTransportSelectSubprotocolCallback subprotocol_callback_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_ONLY_SERVER_SESSION_H_
