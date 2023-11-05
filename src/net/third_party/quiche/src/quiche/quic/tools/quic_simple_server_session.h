// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A toy server specific QuicSession subclass.

#ifndef QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_SESSION_H_
#define QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_SESSION_H_

#include <stdint.h>

#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "quiche/quic/core/http/quic_server_session_base.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/quic/tools/quic_simple_server_stream.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

namespace test {
class QuicSimpleServerSessionPeer;
}  // namespace test

class QuicSimpleServerSession : public QuicServerSessionBase {
 public:
  // Takes ownership of |connection|.
  QuicSimpleServerSession(const QuicConfig& config,
                          const ParsedQuicVersionVector& supported_versions,
                          QuicConnection* connection,
                          QuicSession::Visitor* visitor,
                          QuicCryptoServerStreamBase::Helper* helper,
                          const QuicCryptoServerConfig* crypto_config,
                          QuicCompressedCertsCache* compressed_certs_cache,
                          QuicSimpleServerBackend* quic_simple_server_backend);
  QuicSimpleServerSession(const QuicSimpleServerSession&) = delete;
  QuicSimpleServerSession& operator=(const QuicSimpleServerSession&) = delete;

  ~QuicSimpleServerSession() override;

  // Override base class to detact client sending data on server push stream.
  void OnStreamFrame(const QuicStreamFrame& frame) override;

 protected:
  // QuicSession methods:
  QuicSpdyStream* CreateIncomingStream(QuicStreamId id) override;
  QuicSpdyStream* CreateIncomingStream(PendingStream* pending) override;
  QuicSpdyStream* CreateOutgoingBidirectionalStream() override;
  QuicSimpleServerStream* CreateOutgoingUnidirectionalStream() override;

  // QuicServerSessionBaseMethod:
  std::unique_ptr<QuicCryptoServerStreamBase> CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) override;

  QuicSimpleServerBackend* server_backend() {
    return quic_simple_server_backend_;
  }

  WebTransportHttp3VersionSet LocallySupportedWebTransportVersions()
      const override {
    return quic_simple_server_backend_->SupportsWebTransport()
               ? kDefaultSupportedWebTransportVersions
               : WebTransportHttp3VersionSet();
  }
  HttpDatagramSupport LocalHttpDatagramSupport() override {
    if (ShouldNegotiateWebTransport()) {
      return HttpDatagramSupport::kRfcAndDraft04;
    }
    return QuicServerSessionBase::LocalHttpDatagramSupport();
  }

 private:
  friend class test::QuicSimpleServerSessionPeer;

  QuicSimpleServerBackend* quic_simple_server_backend_;  // Not owned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_SIMPLE_SERVER_SESSION_H_
