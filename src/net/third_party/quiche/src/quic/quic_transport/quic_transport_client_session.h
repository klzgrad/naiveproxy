// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_H_

#include <cstdint>
#include <memory>

#include "url/origin.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_crypto_client_config.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_client_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_server_id.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_protocol.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_session_interface.h"

namespace quic {

// A client session for the QuicTransport protocol.
class QUIC_EXPORT QuicTransportClientSession
    : public QuicSession,
      public QuicTransportSessionInterface {
 public:
  QuicTransportClientSession(QuicConnection* connection,
                             Visitor* owner,
                             const QuicConfig& config,
                             const ParsedQuicVersionVector& supported_versions,
                             const QuicServerId& server_id,
                             QuicCryptoClientConfig* crypto_config,
                             url::Origin origin);

  std::vector<std::string> GetAlpnsToOffer() const override {
    return std::vector<std::string>({QuicTransportAlpn()});
  }

  void CryptoConnect() { crypto_stream_->CryptoConnect(); }

  bool ShouldKeepConnectionAlive() const override { return true; }

  QuicCryptoStream* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }
  const QuicCryptoStream* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

  bool IsSessionReady() const override { return ready_; }

  void OnCryptoHandshakeEvent(CryptoHandshakeEvent event) override;

 protected:
  class ClientIndication : public QuicStream {
   public:
    using QuicStream::QuicStream;

    // This method should never be called, since the stream is client-initiated
    // unidirectional.
    void OnDataAvailable() override {
      QUIC_BUG << "Received data on a write-only stream";
    }
  };

  // Serializes the client indication as described in
  // https://vasilvv.github.io/webtransport/draft-vvv-webtransport-quic.html#rfc.section.3.2
  std::string SerializeClientIndication();
  // Creates the client indication stream and sends the client indication on it.
  void SendClientIndication();

  std::unique_ptr<QuicCryptoClientStream> crypto_stream_;
  url::Origin origin_;
  bool client_indication_sent_ = false;
  bool ready_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_H_
