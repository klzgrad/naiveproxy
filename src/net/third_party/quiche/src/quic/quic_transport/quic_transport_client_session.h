// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_H_

#include <cstdint>
#include <memory>

#include "url/gurl.h"
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
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_protocol.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_session_interface.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_stream.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// A client session for the QuicTransport protocol.
class QUIC_EXPORT_PRIVATE QuicTransportClientSession
    : public QuicSession,
      public QuicTransportSessionInterface {
 public:
  class QUIC_EXPORT_PRIVATE ClientVisitor {
   public:
    virtual ~ClientVisitor() {}

    // Notifies the visitor when the client indication has been sent and the
    // connection is ready to exchange application data.
    virtual void OnSessionReady() = 0;

    // Notifies the visitor when a new stream has been received.  The stream in
    // question can be retrieved using AcceptIncomingBidirectionalStream() or
    // AcceptIncomingUnidirectionalStream().
    virtual void OnIncomingBidirectionalStreamAvailable() = 0;
    virtual void OnIncomingUnidirectionalStreamAvailable() = 0;

    // Notifies the visitor when a new datagram has been received.
    virtual void OnDatagramReceived(quiche::QuicheStringPiece datagram) = 0;

    // Notifies the visitor that a new outgoing stream can now be created.
    virtual void OnCanCreateNewOutgoingBidirectionalStream() = 0;
    virtual void OnCanCreateNewOutgoingUnidirectionalStream() = 0;
  };

  QuicTransportClientSession(QuicConnection* connection,
                             Visitor* owner,
                             const QuicConfig& config,
                             const ParsedQuicVersionVector& supported_versions,
                             const GURL& url,
                             QuicCryptoClientConfig* crypto_config,
                             url::Origin origin,
                             ClientVisitor* visitor);

  std::vector<std::string> GetAlpnsToOffer() const override {
    return std::vector<std::string>({QuicTransportAlpn()});
  }
  void OnAlpnSelected(quiche::QuicheStringPiece alpn) override;
  bool alpn_received() const { return alpn_received_; }

  void CryptoConnect() { crypto_stream_->CryptoConnect(); }

  bool ShouldKeepConnectionAlive() const override { return true; }

  QuicCryptoStream* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }
  const QuicCryptoStream* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

  // Returns true once the encryption has been established and the client
  // indication has been sent.  No application data will be read or written
  // before the connection is ready.  Once the connection becomes ready, this
  // method will never return false.
  bool IsSessionReady() const override { return ready_; }

  QuicStream* CreateIncomingStream(QuicStreamId id) override;
  QuicStream* CreateIncomingStream(PendingStream* /*pending*/) override {
    QUIC_BUG << "QuicTransportClientSession::CreateIncomingStream("
                "PendingStream) not implemented";
    return nullptr;
  }

  void SetDefaultEncryptionLevel(EncryptionLevel level) override;
  void OnOneRttKeysAvailable() override;
  void OnMessageReceived(quiche::QuicheStringPiece message) override;

  // Return the earliest incoming stream that has been received by the session
  // but has not been accepted.  Returns nullptr if there are no incoming
  // streams.
  QuicTransportStream* AcceptIncomingBidirectionalStream();
  QuicTransportStream* AcceptIncomingUnidirectionalStream();

  using QuicSession::CanOpenNextOutgoingBidirectionalStream;
  using QuicSession::CanOpenNextOutgoingUnidirectionalStream;
  QuicTransportStream* OpenOutgoingBidirectionalStream();
  QuicTransportStream* OpenOutgoingUnidirectionalStream();

  using QuicSession::datagram_queue;

 protected:
  class QUIC_EXPORT_PRIVATE ClientIndication : public QuicStream {
   public:
    using QuicStream::QuicStream;

    // This method should never be called, since the stream is client-initiated
    // unidirectional.
    void OnDataAvailable() override {
      QUIC_BUG << "Received data on a write-only stream";
    }
  };

  // Creates and activates a QuicTransportStream for the given ID.
  QuicTransportStream* CreateStream(QuicStreamId id);

  // Serializes the client indication as described in
  // https://vasilvv.github.io/webtransport/draft-vvv-webtransport-quic.html#rfc.section.3.2
  std::string SerializeClientIndication();
  // Creates the client indication stream and sends the client indication on it.
  void SendClientIndication();

  void OnCanCreateNewOutgoingStream(bool unidirectional) override;

  std::unique_ptr<QuicCryptoClientStream> crypto_stream_;
  GURL url_;
  url::Origin origin_;
  ClientVisitor* visitor_;  // not owned
  bool client_indication_sent_ = false;
  bool alpn_received_ = false;
  bool ready_ = false;

  // Contains all of the streams that has been received by the session but have
  // not been processed by the application.
  // TODO(vasilvv): currently, we always send MAX_STREAMS as long as the overall
  // maximum number of streams for the connection has not been exceeded. We
  // should also limit the maximum number of streams that the consuming code
  // has not accepted to a smaller number, by checking the size of
  // |incoming_bidirectional_streams_| and |incoming_unidirectional_streams_|
  // before sending MAX_STREAMS.
  QuicCircularDeque<QuicTransportStream*> incoming_bidirectional_streams_;
  QuicCircularDeque<QuicTransportStream*> incoming_unidirectional_streams_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SESSION_H_
