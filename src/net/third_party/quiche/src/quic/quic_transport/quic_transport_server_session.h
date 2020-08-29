// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_

#include "url/gurl.h"
#include "url/origin.h"
#include "net/third_party/quiche/src/quic/core/quic_connection.h"
#include "net/third_party/quiche/src/quic/core/quic_crypto_server_stream_base.h"
#include "net/third_party/quiche/src/quic/core/quic_session.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_protocol.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_session_interface.h"
#include "net/third_party/quiche/src/quic/quic_transport/quic_transport_stream.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

// A server session for the QuicTransport protocol.
class QUIC_EXPORT_PRIVATE QuicTransportServerSession
    : public QuicSession,
      public QuicTransportSessionInterface {
 public:
  class QUIC_EXPORT_PRIVATE ServerVisitor {
   public:
    virtual ~ServerVisitor() {}

    // Allows the server to decide whether the specified origin is allowed to
    // connect to it.
    virtual bool CheckOrigin(url::Origin origin) = 0;

    // Indicates that the server received a path parameter from the client.  The
    // path parameter is parsed, and can be retrived from url.path() and
    // url.query().  If this method returns false, the connection is closed.
    virtual bool ProcessPath(const GURL& url) = 0;
  };

  QuicTransportServerSession(QuicConnection* connection,
                             Visitor* owner,
                             const QuicConfig& config,
                             const ParsedQuicVersionVector& supported_versions,
                             const QuicCryptoServerConfig* crypto_config,
                             QuicCompressedCertsCache* compressed_certs_cache,
                             ServerVisitor* visitor);

  std::vector<quiche::QuicheStringPiece>::const_iterator SelectAlpn(
      const std::vector<quiche::QuicheStringPiece>& alpns) const override {
    return std::find(alpns.cbegin(), alpns.cend(), QuicTransportAlpn());
  }

  bool ShouldKeepConnectionAlive() const override { return true; }

  QuicCryptoServerStreamBase* GetMutableCryptoStream() override {
    return crypto_stream_.get();
  }
  const QuicCryptoServerStreamBase* GetCryptoStream() const override {
    return crypto_stream_.get();
  }

  // Returns true once the encryption has been established, the client
  // indication has been received and the origin has been verified.  No
  // application data will be read or written before the connection is ready.
  // Once the connection becomes ready, this method will never return false.
  bool IsSessionReady() const override { return ready_; }

  QuicStream* CreateIncomingStream(QuicStreamId id) override;
  QuicStream* CreateIncomingStream(PendingStream* /*pending*/) override {
    QUIC_BUG << "QuicTransportServerSession::CreateIncomingStream("
                "PendingStream) not implemented";
    return nullptr;
  }

 protected:
  class QUIC_EXPORT_PRIVATE ClientIndication : public QuicStream {
   public:
    explicit ClientIndication(QuicTransportServerSession* session);
    void OnDataAvailable() override;

   private:
    QuicTransportServerSession* session_;
    std::string buffer_;
  };

  // Utility class for parsing the client indication.
  class QUIC_EXPORT_PRIVATE ClientIndicationParser {
   public:
    ClientIndicationParser(QuicTransportServerSession* session,
                           quiche::QuicheStringPiece indication)
        : session_(session), reader_(indication) {}

    // Parses the specified indication.  Automatically closes the connection
    // with detailed error if parsing fails.  Returns true on success, false on
    // failure.
    bool Parse();

   private:
    void Error(const std::string& error_message);
    void ParseError(quiche::QuicheStringPiece error_message);

    // Processes the path portion of the client indication.
    bool ProcessPath(quiche::QuicheStringPiece path);

    QuicTransportServerSession* session_;
    QuicDataReader reader_;
  };

  // Parses and processes the client indication as described in
  // https://vasilvv.github.io/webtransport/draft-vvv-webtransport-quic.html#rfc.section.3.2
  void ProcessClientIndication(quiche::QuicheStringPiece indication);

  virtual void OnIncomingDataStream(QuicTransportStream* /*stream*/) {}

  std::unique_ptr<QuicCryptoServerStreamBase> crypto_stream_;
  bool ready_ = false;
  ServerVisitor* visitor_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_SERVER_SESSION_H_
